#include "scoreboard.h"

#include <string.h>

static void remember(struct scoreboard *board)
{
	if (board->history_count == SCOREBOARD_HISTORY) {
		memmove(board->history, board->history + 1, (SCOREBOARD_HISTORY - 1) * sizeof(*board->history));
		--board->history_count;
	}
	board->history[board->history_count++] = board->state;
}

static void clear_count(struct scoreboard_state *state)
{
	state->balls = 0;
	state->strikes = 0;
}

static void next_half(struct scoreboard_state *state)
{
	clear_count(state);
	state->outs = 0;
	if (state->bottom) {
		state->bottom = false;
		if (state->inning + 1 < SCOREBOARD_INNINGS)
			++state->inning;
	} else {
		state->bottom = true;
	}
}

static bool previous_half(struct scoreboard_state *state)
{
	clear_count(state);
	state->outs = 0;
	if (state->bottom) {
		state->bottom = false;
		return true;
	}
	if (state->inning == 0)
		return false;
	--state->inning;
	state->bottom = true;
	return true;
}

void scoreboard_init(struct scoreboard *board)
{
	memset(board, 0, sizeof(*board));
}

bool scoreboard_apply(struct scoreboard *board, enum scoreboard_action action)
{
	if (!board)
		return false;
	remember(board);

	switch (action) {
	case SCOREBOARD_BALL:
		if (++board->state.balls == 4)
			clear_count(&board->state);
		break;
	case SCOREBOARD_STRIKE:
		if (++board->state.strikes == 3) {
			clear_count(&board->state);
			if (++board->state.outs == 3)
				next_half(&board->state);
		}
		break;
	case SCOREBOARD_OUT:
		clear_count(&board->state);
		if (++board->state.outs == 3)
			next_half(&board->state);
		break;
	case SCOREBOARD_AWAY_RUN:
		++board->state.runs[0][board->state.inning];
		break;
	case SCOREBOARD_HOME_RUN:
		++board->state.runs[1][board->state.inning];
		break;
	case SCOREBOARD_NEXT_HALF:
		next_half(&board->state);
		break;
	case SCOREBOARD_RESET_COUNT:
		clear_count(&board->state);
		break;
	case SCOREBOARD_AWAY_RUN_REMOVE:
	case SCOREBOARD_HOME_RUN_REMOVE: {
		const unsigned team = action == SCOREBOARD_AWAY_RUN_REMOVE ? 0 : 1;
		if (!board->state.runs[team][board->state.inning]) {
			--board->history_count;
			return false;
		}
		--board->state.runs[team][board->state.inning];
		break;
	}
	case SCOREBOARD_PREVIOUS_HALF:
		if (!previous_half(&board->state)) {
			--board->history_count;
			return false;
		}
		break;
	default:
		--board->history_count;
		return false;
	}
	return true;
}

bool scoreboard_undo(struct scoreboard *board)
{
	if (!board || !board->history_count)
		return false;
	board->state = board->history[--board->history_count];
	return true;
}

unsigned scoreboard_total(const struct scoreboard_state *state, unsigned team)
{
	unsigned total = 0;
	if (!state || team > 1)
		return 0;
	for (size_t inning = 0; inning < SCOREBOARD_INNINGS; ++inning)
		total += state->runs[team][inning];
	return total;
}
