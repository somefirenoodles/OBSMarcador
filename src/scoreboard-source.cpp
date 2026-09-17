#include <obs-module.h>
#include <graphics/graphics.h>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <gdiplus.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <mutex>
#include <string>
#include <vector>

extern "C" {
#include "scoreboard.h"
}

using namespace Gdiplus;

namespace {
constexpr uint32_t WIDTH = 1280;
constexpr uint32_t HEIGHT = 720;
constexpr float RUN_FLASH_SECONDS = 0.6f;
constexpr float OUT_FLASH_SECONDS = 0.45f;
constexpr float INNING_FLASH_SECONDS = 1.1f;
static_assert(WIDTH * 9 == HEIGHT * 16);
ULONG_PTR gdiplus_token;

std::wstring wide(const char *text)
{
	if (!text || !*text)
		return {};
	const int count = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
	if (count <= 1)
		return {};
	std::wstring value(static_cast<size_t>(count), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, text, -1, value.data(), count);
	value.pop_back();
	return value;
}

class ScoreboardSource {
public:
	obs_source_t *source;
	struct scoreboard board{};
	std::string away = "VISITANTE";
	std::string home = "LOCAL";
	gs_texture_t *texture = nullptr;
	std::vector<uint8_t> pixels;
	bool texture_dirty = false;
	float run_flash = 0.0f;
	float out_flash = 0.0f;
	float inning_flash = 0.0f;
	unsigned flash_team = 0;
	std::mutex mutex;
	std::array<obs_hotkey_id, 10> hotkeys{};

	ScoreboardSource(obs_data_t *settings, obs_source_t *source_) : source(source_)
	{
		scoreboard_init(&board);
		update(settings);
		register_hotkeys();
	}

	~ScoreboardSource()
	{
		obs_enter_graphics();
		if (texture)
			gs_texture_destroy(texture);
		obs_leave_graphics();
	}

	void register_hotkeys()
	{
		static const char *names[] = {"Softball.Ball",          "Softball.Strike",
					      "Softball.Out",           "Softball.AwayRun",
					      "Softball.HomeRun",       "Softball.NextHalf",
					      "Softball.Undo",          "Softball.AwayRunRemove",
					      "Softball.HomeRunRemove", "Softball.PreviousHalf"};
		static const char *labels[] = {"ScoreboardBall",          "ScoreboardStrike",
					       "ScoreboardOut",           "ScoreboardAwayRun",
					       "ScoreboardHomeRun",       "ScoreboardNextHalf",
					       "ScoreboardUndo",          "ScoreboardAwayRunRemove",
					       "ScoreboardHomeRunRemove", "ScoreboardPreviousHalf"};
		static const obs_key_t keys[] = {OBS_KEY_NUM1, OBS_KEY_NUM2, OBS_KEY_NUM3, OBS_KEY_NUM4, OBS_KEY_NUM6,
						 OBS_KEY_NUM5, OBS_KEY_NUM0, OBS_KEY_NUM7, OBS_KEY_NUM8, OBS_KEY_NUM9};
		for (size_t i = 0; i < hotkeys.size(); ++i) {
			hotkeys[i] =
				obs_hotkey_register_source(source, names[i], obs_module_text(labels[i]), hotkey, this);
			obs_key_combination_t key{0, keys[i]};
			obs_hotkey_load_bindings(hotkeys[i], &key, 1);
		}
	}

	static void hotkey(void *data, obs_hotkey_id id, obs_hotkey_t *, bool pressed)
	{
		if (!pressed)
			return;
		auto *self = static_cast<ScoreboardSource *>(data);
		for (size_t i = 0; i < self->hotkeys.size(); ++i) {
			if (self->hotkeys[i] == id) {
				if (i == 6)
					self->undo();
				else
					self->apply(static_cast<scoreboard_action>(i));
				return;
			}
		}
	}

	void apply(scoreboard_action action)
	{
		std::lock_guard<std::mutex> lock(mutex);
		const unsigned previous_outs = board.state.outs;
		const unsigned previous_inning = board.state.inning;
		const bool previous_bottom = board.state.bottom;
		if (scoreboard_apply(&board, action)) {
			if (action == SCOREBOARD_AWAY_RUN || action == SCOREBOARD_HOME_RUN) {
				flash_team = action == SCOREBOARD_AWAY_RUN ? 0 : 1;
				run_flash = RUN_FLASH_SECONDS;
			}
			if (board.state.inning != previous_inning || board.state.bottom != previous_bottom)
				inning_flash = INNING_FLASH_SECONDS;
			else if (board.state.outs != previous_outs)
				out_flash = OUT_FLASH_SECONDS;
			save();
			render();
		}
	}

	void tick(float seconds)
	{
		std::lock_guard<std::mutex> lock(mutex);
		if (run_flash <= 0.0f && out_flash <= 0.0f && inning_flash <= 0.0f)
			return;
		run_flash = std::max(0.0f, run_flash - seconds);
		out_flash = std::max(0.0f, out_flash - seconds);
		inning_flash = std::max(0.0f, inning_flash - seconds);
		render();
	}

	void undo()
	{
		std::lock_guard<std::mutex> lock(mutex);
		if (scoreboard_undo(&board)) {
			save();
			render();
		}
	}

	void save()
	{
		obs_data_t *settings = obs_source_get_settings(source);
		obs_data_set_int(settings, "balls", board.state.balls);
		obs_data_set_int(settings, "strikes", board.state.strikes);
		obs_data_set_int(settings, "outs", board.state.outs);
		obs_data_set_int(settings, "inning", board.state.inning);
		obs_data_set_bool(settings, "bottom", board.state.bottom);
		for (unsigned team = 0; team < 2; ++team)
			for (unsigned inning = 0; inning < SCOREBOARD_INNINGS; ++inning) {
				char key[24];
				snprintf(key, sizeof(key), "runs_%u_%u", team, inning);
				obs_data_set_int(settings, key, board.state.runs[team][inning]);
			}
		obs_data_release(settings);
	}

	void update(obs_data_t *settings)
	{
		std::lock_guard<std::mutex> lock(mutex);
		away = obs_data_get_string(settings, "away_name");
		home = obs_data_get_string(settings, "home_name");
		board.state.balls =
			static_cast<unsigned char>(std::clamp(obs_data_get_int(settings, "balls"), 0LL, 3LL));
		board.state.strikes =
			static_cast<unsigned char>(std::clamp(obs_data_get_int(settings, "strikes"), 0LL, 2LL));
		board.state.outs = static_cast<unsigned char>(std::clamp(obs_data_get_int(settings, "outs"), 0LL, 2LL));
		board.state.inning = static_cast<unsigned char>(
			std::clamp(obs_data_get_int(settings, "inning"), 0LL, SCOREBOARD_INNINGS - 1LL));
		board.state.bottom = obs_data_get_bool(settings, "bottom");
		for (unsigned team = 0; team < 2; ++team)
			for (unsigned inning = 0; inning < SCOREBOARD_INNINGS; ++inning) {
				char key[24];
				snprintf(key, sizeof(key), "runs_%u_%u", team, inning);
				board.state.runs[team][inning] =
					static_cast<unsigned short>(obs_data_get_int(settings, key));
			}
		render();
	}

	void draw_text(Graphics &graphics, const std::wstring &text, const RectF &box, float size, const Color &color,
		       StringAlignment alignment = StringAlignmentCenter)
	{
		FontFamily family(L"Segoe UI");
		Font font(&family, size, FontStyleBold, UnitPixel);
		StringFormat format;
		format.SetAlignment(alignment);
		format.SetLineAlignment(StringAlignmentCenter);
		SolidBrush brush(color);
		graphics.DrawString(text.c_str(), -1, &font, box, &format, &brush);
	}

	void render()
	{
		Bitmap bitmap(WIDTH, HEIGHT, PixelFormat32bppPARGB);
		Graphics graphics(&bitmap);
		graphics.SetSmoothingMode(SmoothingModeAntiAlias);
		graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
		graphics.Clear(Color(255, 255, 255, 255));

		SolidBrush panel(Color(255, 255, 255, 255));
		SolidBrush active(Color(255, 220, 220, 220));
		Pen line(Color(150, 0, 0, 0), 2.0f);
		graphics.FillRectangle(&panel, Rect(0, 0, WIDTH, HEIGHT));

		const Color ink(255, 0, 0, 0);
		const Color muted(255, 55, 55, 55);
		draw_text(graphics, L"BOLA", RectF(35, 38, 115, 65), 30, muted);
		draw_text(graphics, std::to_wstring(board.state.balls), RectF(145, 38, 55, 65), 42, ink);
		draw_text(graphics, L"STRIKE", RectF(220, 38, 135, 65), 30, muted);
		draw_text(graphics, std::to_wstring(board.state.strikes), RectF(350, 38, 55, 65), 42, ink);
		const float out_pulse =
			out_flash > 0.0f ? std::sin((1.0f - out_flash / OUT_FLASH_SECONDS) * 3.14159265f) : 0.0f;
		if (out_pulse > 0.0f) {
			SolidBrush out_glow(Color(static_cast<BYTE>(190 * out_pulse), 255, 40, 40));
			graphics.FillRectangle(&out_glow, RectF(415, 25, 165, 90));
		}
		draw_text(graphics, L"OUT", RectF(425, 38, 90, 65), 30 + 5 * out_pulse, muted);
		draw_text(graphics, std::to_wstring(board.state.outs), RectF(510, 38, 55, 65), 42 + 12 * out_pulse,
			  ink);
		std::wstring half = board.state.bottom ? L"BAJA " : L"ALTA ";
		draw_text(graphics, half + std::to_wstring(board.state.inning + 1), RectF(930, 38, 300, 65), 38, ink);

		constexpr float name_x = 15, name_w = 250, cell_w = 130, header_y = 170, row_h = 190;
		for (unsigned inning = 0; inning < SCOREBOARD_INNINGS; ++inning) {
			const float x = name_x + name_w + inning * cell_w;
			if (inning == board.state.inning)
				graphics.FillRectangle(&active, RectF(x + 5, header_y, cell_w - 10, 38));
			draw_text(graphics, std::to_wstring(inning + 1), RectF(x, header_y, cell_w, 38), 23, ink);
		}
		draw_text(graphics, L"TOTAL", RectF(name_x + name_w + SCOREBOARD_INNINGS * cell_w, header_y, 220, 55),
			  25, muted);

		for (unsigned team = 0; team < 2; ++team) {
			const float y = 230 + team * row_h;
			const float pulse = run_flash > 0.0f && team == flash_team
						    ? std::sin((1.0f - run_flash / RUN_FLASH_SECONDS) * 3.14159265f)
						    : 0.0f;
			if (pulse > 0.0f) {
				SolidBrush celebration(Color(static_cast<BYTE>(150 * pulse), 255, 176, 32));
				graphics.FillRectangle(&celebration, RectF(0, y, WIDTH, row_h));
			}
			draw_text(graphics, wide(team == 0 ? away.c_str() : home.c_str()),
				  RectF(name_x, y, name_w - 15, row_h), 36, ink, StringAlignmentNear);
			for (unsigned inning = 0; inning < SCOREBOARD_INNINGS; ++inning) {
				const float x = name_x + name_w + inning * cell_w;
				draw_text(graphics, std::to_wstring(board.state.runs[team][inning]),
					  RectF(x, y, cell_w, row_h), 50, ink);
			}
			draw_text(graphics, std::to_wstring(scoreboard_total(&board.state, team)),
				  RectF(name_x + name_w + SCOREBOARD_INNINGS * cell_w, y, 220, row_h), 58 + 14 * pulse,
				  ink);
		}
		graphics.DrawLine(&line, name_x, 225.0f, static_cast<float>(WIDTH - 15), 225.0f);
		graphics.DrawLine(&line, name_x, 420.0f, static_cast<float>(WIDTH - 15), 420.0f);

		if (inning_flash > 0.0f) {
			const float pulse = std::sin((1.0f - inning_flash / INNING_FLASH_SECONDS) * 3.14159265f);
			SolidBrush shade(Color(static_cast<BYTE>(230 * pulse), 235, 235, 235));
			graphics.FillRectangle(&shade, RectF(0, 210, WIDTH, 300));
			const std::wstring change = board.state.bottom ? L"CAMBIO · BAJA " : L"CAMBIO · ALTA ";
			draw_text(graphics, change + std::to_wstring(board.state.inning + 1), RectF(0, 210, WIDTH, 300),
				  70 + 14 * pulse, Color(static_cast<BYTE>(255 * pulse), 0, 0, 0));
		}

		BitmapData bits{};
		Rect bounds(0, 0, WIDTH, HEIGHT);
		if (bitmap.LockBits(&bounds, ImageLockModeRead, PixelFormat32bppPARGB, &bits) != Ok)
			return;
		const auto *source_pixels = static_cast<const uint8_t *>(bits.Scan0);
		pixels.resize(WIDTH * HEIGHT * 4);
		for (uint32_t y = 0; y < HEIGHT; ++y)
			memcpy(pixels.data() + y * WIDTH * 4, source_pixels + y * bits.Stride, WIDTH * 4);
		texture_dirty = true;
		bitmap.UnlockBits(&bits);
	}
};

bool property_action(obs_properties_t *, obs_property_t *property, void *data)
{
	auto *source = static_cast<ScoreboardSource *>(data);
	const char *name = obs_property_name(property);
	if (strcmp(name, "undo") == 0)
		source->undo();
	else if (strcmp(name, "ball") == 0)
		source->apply(SCOREBOARD_BALL);
	else if (strcmp(name, "strike") == 0)
		source->apply(SCOREBOARD_STRIKE);
	else if (strcmp(name, "out") == 0)
		source->apply(SCOREBOARD_OUT);
	else if (strcmp(name, "away_run") == 0)
		source->apply(SCOREBOARD_AWAY_RUN);
	else if (strcmp(name, "home_run") == 0)
		source->apply(SCOREBOARD_HOME_RUN);
	else if (strcmp(name, "away_run_remove") == 0)
		source->apply(SCOREBOARD_AWAY_RUN_REMOVE);
	else if (strcmp(name, "home_run_remove") == 0)
		source->apply(SCOREBOARD_HOME_RUN_REMOVE);
	else if (strcmp(name, "next_half") == 0)
		source->apply(SCOREBOARD_NEXT_HALF);
	else if (strcmp(name, "previous_half") == 0)
		source->apply(SCOREBOARD_PREVIOUS_HALF);
	return true;
}

obs_properties_t *properties(void *data)
{
	obs_properties_t *props = obs_properties_create();
	obs_properties_add_text(props, "away_name", obs_module_text("AwayName"), OBS_TEXT_DEFAULT);
	obs_properties_add_text(props, "home_name", obs_module_text("HomeName"), OBS_TEXT_DEFAULT);
	obs_properties_add_button2(props, "ball", obs_module_text("Ball"), property_action, data);
	obs_properties_add_button2(props, "strike", obs_module_text("Strike"), property_action, data);
	obs_properties_add_button2(props, "out", obs_module_text("Out"), property_action, data);
	obs_properties_add_button2(props, "away_run", obs_module_text("AwayRun"), property_action, data);
	obs_properties_add_button2(props, "away_run_remove", obs_module_text("AwayRunRemove"), property_action, data);
	obs_properties_add_button2(props, "home_run", obs_module_text("HomeRun"), property_action, data);
	obs_properties_add_button2(props, "home_run_remove", obs_module_text("HomeRunRemove"), property_action, data);
	obs_properties_add_button2(props, "previous_half", obs_module_text("PreviousHalf"), property_action, data);
	obs_properties_add_button2(props, "next_half", obs_module_text("NextHalf"), property_action, data);
	obs_properties_add_button2(props, "undo", obs_module_text("Undo"), property_action, data);
	return props;
}
} // namespace

extern "C" bool scoreboard_source_register(void)
{
	GdiplusStartupInput input;
	if (GdiplusStartup(&gdiplus_token, &input, nullptr) != Ok)
		return false;

	obs_source_info info{};
	info.id = "softball_scoreboard";
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW | OBS_SOURCE_SRGB;
	info.get_name = [](void *) {
		return obs_module_text("SoftballScoreboard");
	};
	info.create = [](obs_data_t *settings, obs_source_t *source) -> void * {
		return new ScoreboardSource(settings, source);
	};
	info.destroy = [](void *data) {
		delete static_cast<ScoreboardSource *>(data);
	};
	info.update = [](void *data, obs_data_t *settings) {
		static_cast<ScoreboardSource *>(data)->update(settings);
	};
	info.get_width = [](void *) {
		return WIDTH;
	};
	info.get_height = [](void *) {
		return HEIGHT;
	};
	info.get_properties = properties;
	info.get_defaults = [](obs_data_t *settings) {
		obs_data_set_default_string(settings, "away_name", "VISITANTE");
		obs_data_set_default_string(settings, "home_name", "LOCAL");
	};
	info.video_render = [](void *data, gs_effect_t *) {
		auto *scoreboard = static_cast<ScoreboardSource *>(data);
		std::lock_guard<std::mutex> lock(scoreboard->mutex);
		if (scoreboard->texture_dirty) {
			const uint8_t *pixels = scoreboard->pixels.data();
			if (!scoreboard->texture)
				scoreboard->texture = gs_texture_create(WIDTH, HEIGHT, GS_BGRA, 1, &pixels, GS_DYNAMIC);
			else
				gs_texture_set_image(scoreboard->texture, pixels, WIDTH * 4, false);
			scoreboard->texture_dirty = false;
		}
		if (scoreboard->texture) {
			gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
			while (gs_effect_loop(effect, "Draw"))
				obs_source_draw(scoreboard->texture, 0, 0, WIDTH, HEIGHT, false);
		}
	};
	info.video_tick = [](void *data, float seconds) {
		static_cast<ScoreboardSource *>(data)->tick(seconds);
	};
	obs_register_source(&info);
	return true;
}

extern "C" void scoreboard_source_unregister(void)
{
	if (gdiplus_token)
		GdiplusShutdown(gdiplus_token);
}
