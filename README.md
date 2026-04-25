# Mixed-Signals
MIXED SIGNALS - 2 PLAYER TCP WORD GAME
MIXED SIGNALS - 2 PLAYER TCP WORD GAME

Members:
- Nicko R. Reorizo
- Raphael Andrei V. Seguenza
- Syed Kxien Yzion D. Abelado

FILES:
- mixed_signals_server.c
- mixed_signals_client.c

GAME FEATURES IMPLEMENTED:
1. Game Title: Mixed Signals
2. Turn-based 2-player gameplay
3. Player 1 / Player 2 switch roles every round
4. Distortion modes:
   - Scramble letters
   - Remove some letters
   - Reverse the word
5. Time limit for guessing (default: 12 seconds)
6. Risk or Pass mechanic:
   - Guess immediately for full points
   - PASS once for a new distortion but fewer points
7. Scoring:
   - Correct guess = guesser earns points
   - Wrong guess or timeout = sender earns points
   - Fast correct answer (within 5 seconds) = speed bonus
8. Fixed number of rounds (default: 5)

COMPILE:
gcc -Wall -Wextra -std=c11 mixed_signals_server.c -o mixed_signals_server
gcc -Wall -Wextra -std=c11 mixed_signals_client.c -o mixed_signals_client

RUN:
1. Start the server first:
   ./mixed_signals_server 51717

   Or with custom rounds and time limit:
   ./mixed_signals_server 51717 6 15

   Format:
   ./mixed_signals_server <port> [rounds] [time_limit_seconds]

2. Open two terminals for the clients.

3. Start client 1:
   ./mixed_signals_client localhost 51717

4. Start client 2:
   ./mixed_signals_client localhost 51717

HOW IT WORKS:
- In odd rounds, Player 1 sends the secret word and Player 2 guesses.
- In even rounds, Player 2 sends the secret word and Player 1 guesses.
- The server applies a random distortion.
- The guesser may answer directly or type PASS.
- PASS gives a second distortion and a final chance, but for fewer points.

DEFAULT SCORING:
- Correct guess without PASS = 10 points
- Correct guess after PASS = 5 points
- Correct guess within 5 seconds = +2 bonus points
- Wrong guess = sender gets 10 points
- Timeout = sender gets 10 points

NOTES:
- Input word must be one word using letters only.
- This is a TCP socket-based implementation inspired by the client-server reference.
- Best tested on Linux or WSL.
