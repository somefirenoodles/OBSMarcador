#include "scoreboard.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
	struct scoreboard board;
	scoreboard_init(&board);
	assert(board.state.inning == 0 && !board.state.bottom);

	for (int i = 0; i < 4; ++i)
		assert(scoreboard_apply(&board, SCOREBOARD_BALL));
	assert(board.state.balls == 0 && board.state.strikes == 0);

	for (int i = 0; i < 3; ++i)
		assert(scoreboard_apply(&board, SCOREBOARD_STRIKE));
	assert(board.state.outs == 1 && board.state.strikes == 0);

	assert(scoreboard_apply(&board, SCOREBOARD_AWAY_RUN));
	assert(scoreboard_total(&board.state, 0) == 1);
	assert(scoreboard_apply(&board, SCOREBOARD_AWAY_RUN_REMOVE));
	assert(scoreboard_total(&board.state, 0) == 0);
	assert(!scoreboard_apply(&board, SCOREBOARD_AWAY_RUN_REMOVE));
	assert(scoreboard_total(&board.state, 0) == 0);
	assert(scoreboard_apply(&board, SCOREBOARD_HOME_RUN));
	assert(scoreboard_apply(&board, SCOREBOARD_HOME_RUN_REMOVE));
	assert(scoreboard_total(&board.state, 1) == 0);
	assert(scoreboard_apply(&board, SCOREBOARD_AWAY_RUN));
	assert(scoreboard_undo(&board));
	assert(scoreboard_total(&board.state, 0) == 0);

	for (int i = board.state.outs; i < 3; ++i)
		assert(scoreboard_apply(&board, SCOREBOARD_OUT));
	assert(board.state.bottom && board.state.inning == 0 && board.state.outs == 0);
	for (int i = 0; i < 3; ++i)
		assert(scoreboard_apply(&board, SCOREBOARD_OUT));
	assert(!board.state.bottom && board.state.inning == 1);
	assert(scoreboard_apply(&board, SCOREBOARD_PREVIOUS_HALF));
	assert(board.state.bottom && board.state.inning == 0);
	assert(scoreboard_apply(&board, SCOREBOARD_PREVIOUS_HALF));
	assert(!board.state.bottom && board.state.inning == 0);
	assert(!scoreboard_apply(&board, SCOREBOARD_PREVIOUS_HALF));

	assert(SCOREBOARD_INNINGS == 6);

	puts("scoreboard logic: ok");
	return 0;
}
