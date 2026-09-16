#include <obs-module.h>
#include <graphics/graphics.h>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <gdiplus.h>

#include <algorithm>
#include <array>
#include <mutex>
#include <string>
#include <vector>

extern "C" {
#include "scoreboard.h"
}

using namespace Gdiplus;

namespace {
constexpr uint32_t WIDTH = 1200;
constexpr uint32_t HEIGHT = 360;
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
	std::mutex mutex;
	std::array<obs_hotkey_id, 7> hotkeys{};

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
		static const char *names[] = {"Softball.Ball", "Softball.Strike", "Softball.Out", "Softball.AwayRun",
					      "Softball.HomeRun", "Softball.NextHalf", "Softball.Undo"};
		static const char *labels[] = {"ScoreboardBall", "ScoreboardStrike", "ScoreboardOut", "ScoreboardAwayRun",
					       "ScoreboardHomeRun", "ScoreboardNextHalf", "ScoreboardUndo"};
		static const obs_key_t keys[] = {OBS_KEY_NUM1, OBS_KEY_NUM2, OBS_KEY_NUM3, OBS_KEY_NUM4,
					     OBS_KEY_NUM6, OBS_KEY_NUM5, OBS_KEY_NUM0};
		for (size_t i = 0; i < hotkeys.size(); ++i) {
			hotkeys[i] = obs_hotkey_register_source(source, names[i], obs_module_text(labels[i]), hotkey, this);
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
		if (scoreboard_apply(&board, action)) {
			save();
			render();
		}
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
		board.state.balls = static_cast<unsigned char>(std::clamp(obs_data_get_int(settings, "balls"), 0LL, 3LL));
		board.state.strikes = static_cast<unsigned char>(std::clamp(obs_data_get_int(settings, "strikes"), 0LL, 2LL));
		board.state.outs = static_cast<unsigned char>(std::clamp(obs_data_get_int(settings, "outs"), 0LL, 2LL));
		board.state.inning = static_cast<unsigned char>(std::clamp(obs_data_get_int(settings, "inning"), 0LL, 6LL));
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
		graphics.Clear(Color(0, 0, 0, 0));

		SolidBrush panel(Color(225, 13, 18, 27));
		SolidBrush active(Color(255, 213, 50, 50));
		Pen line(Color(110, 255, 255, 255), 2.0f);
		graphics.FillRectangle(&panel, Rect(0, 0, WIDTH, HEIGHT));

		const Color white(255, 245, 247, 250);
		const Color muted(255, 180, 190, 205);
		const Color red(255, 255, 91, 91);
		draw_text(graphics, L"BOLA", RectF(35, 18, 115, 50), 27, muted);
		draw_text(graphics, std::to_wstring(board.state.balls), RectF(145, 18, 55, 50), 35, red);
		draw_text(graphics, L"STRIKE", RectF(220, 18, 135, 50), 27, muted);
		draw_text(graphics, std::to_wstring(board.state.strikes), RectF(350, 18, 55, 50), 35, red);
		draw_text(graphics, L"OUT", RectF(425, 18, 90, 50), 27, muted);
		draw_text(graphics, std::to_wstring(board.state.outs), RectF(510, 18, 55, 50), 35, red);
		std::wstring half = board.state.bottom ? L"BAJA " : L"ALTA ";
		draw_text(graphics, half + std::to_wstring(board.state.inning + 1), RectF(880, 18, 270, 50), 30, white);

		constexpr float name_x = 30, name_w = 260, cell_w = 105, header_y = 92, row_h = 88;
		for (unsigned inning = 0; inning < SCOREBOARD_INNINGS; ++inning) {
			const float x = name_x + name_w + inning * cell_w;
			if (inning == board.state.inning)
				graphics.FillRectangle(&active, RectF(x + 5, header_y, cell_w - 10, 38));
			draw_text(graphics, std::to_wstring(inning + 1), RectF(x, header_y, cell_w, 38), 23, white);
		}
		draw_text(graphics, L"TOTAL", RectF(name_x + name_w + 7 * cell_w, header_y, cell_w + 60, 38), 21, muted);

		for (unsigned team = 0; team < 2; ++team) {
			const float y = 135 + team * row_h;
			draw_text(graphics, wide(team == 0 ? away.c_str() : home.c_str()), RectF(name_x, y, name_w - 15, row_h),
				  30, white, StringAlignmentNear);
			for (unsigned inning = 0; inning < SCOREBOARD_INNINGS; ++inning) {
				const float x = name_x + name_w + inning * cell_w;
				draw_text(graphics, std::to_wstring(board.state.runs[team][inning]), RectF(x, y, cell_w, row_h),
					  37, inning == board.state.inning ? red : white);
			}
			draw_text(graphics, std::to_wstring(scoreboard_total(&board.state, team)),
				  RectF(name_x + name_w + 7 * cell_w, y, cell_w + 60, row_h), 42, red);
		}
		graphics.DrawLine(&line, name_x, 132.0f, static_cast<float>(WIDTH - 30), 132.0f);
		graphics.DrawLine(&line, name_x, 223.0f, static_cast<float>(WIDTH - 30), 223.0f);

		BitmapData bits{};
		Rect bounds(0, 0, WIDTH, HEIGHT);
		if (bitmap.LockBits(&bounds, ImageLockModeRead, PixelFormat32bppPARGB, &bits) != Ok)
			return;
		const uint8_t *pixels = static_cast<uint8_t *>(bits.Scan0);
		obs_enter_graphics();
		if (!texture)
			texture = gs_texture_create(WIDTH, HEIGHT, GS_BGRA, 1, &pixels, GS_DYNAMIC);
		else
			gs_texture_set_image(texture, pixels, static_cast<uint32_t>(bits.Stride), false);
		obs_leave_graphics();
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
	else if (strcmp(name, "next_half") == 0)
		source->apply(SCOREBOARD_NEXT_HALF);
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
	obs_properties_add_button2(props, "home_run", obs_module_text("HomeRun"), property_action, data);
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
	info.get_name = [](void *) { return obs_module_text("SoftballScoreboard"); };
	info.create = [](obs_data_t *settings, obs_source_t *source) -> void * {
		return new ScoreboardSource(settings, source);
	};
	info.destroy = [](void *data) { delete static_cast<ScoreboardSource *>(data); };
	info.update = [](void *data, obs_data_t *settings) { static_cast<ScoreboardSource *>(data)->update(settings); };
	info.get_width = [](void *) { return WIDTH; };
	info.get_height = [](void *) { return HEIGHT; };
	info.get_properties = properties;
	info.get_defaults = [](obs_data_t *settings) {
		obs_data_set_default_string(settings, "away_name", "VISITANTE");
		obs_data_set_default_string(settings, "home_name", "LOCAL");
	};
	info.video_render = [](void *data, gs_effect_t *) {
		auto *scoreboard = static_cast<ScoreboardSource *>(data);
		std::lock_guard<std::mutex> lock(scoreboard->mutex);
		if (scoreboard->texture)
			obs_source_draw(scoreboard->texture, 0, 0, WIDTH, HEIGHT, false);
	};
	obs_register_source(&info);
	return true;
}

extern "C" void scoreboard_source_unregister(void)
{
	if (gdiplus_token)
		GdiplusShutdown(gdiplus_token);
}
