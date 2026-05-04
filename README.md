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

Use one PC as the server PC, and the other PC as a client PC.

On PC 1: Server
Run:

cd "/mnt/c/Users/DC Gaming/Desktop/OS_GAME"
./server 5000
Then find PC 1’s IP address.

On Windows PowerShell:

ipconfig
Look for something like:

IPv4 Address . . . . . . . . . . : 192.168.1.25
That 192.168.x.x number is the server IP.

On PC 1: Player 1 Client
Open another terminal on PC 1:

cd "/mnt/c/Users/DC Gaming/Desktop/OS_GAME"
./client localhost 5000
On PC 2: Player 2 Client
Copy/compile the client program on PC 2, then run:

./client 192.168.1.25 5000
Replace 192.168.1.25 with PC 1’s real IPv4 address.

Both PCs must be on the same Wi-Fi/LAN. If PC 2 cannot connect, allow port 5000 through Windows Firewall on PC 1. Also make sure the server is already running before the clients connect.
