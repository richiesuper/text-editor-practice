/***** README *****/

// This source file is categorized into "sections"
// Sections are marked with these headers
	/***** <SECTION_NAME> *****/

/***** INCLUDES *****/

// Feature Test Macros
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

// Actual Includes
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

/***** DEFINES *****/

#define EDITOR_VERSION "4.20.69"
#define CTRL_KEY(k) ((k) & 0x1f)

enum EditorKey {
	ARROW_LEFT = 1000,
	ARROW_RIGHT,
	ARROW_UP,
	ARROW_DOWN,
	DEL,
	HOME,
	END,
	PAGE_UP,
	PAGE_DOWN
};

/***** DATA *****/

struct EditorRow {
	int size;
	char* chars;
};

struct EditorConfig {
	struct termios origTermios; // Struct 'termios' named origTermios which contains fields defined in termios.h

	int screenRows; // Number of terminal rows
	int screenCols; // Number of terminal collumns

	int curx; // Cursor x position
	int cury; // Cursor y position

	int rowOffset;

	int numRows;
	struct EditorRow* row;
} ec;

/***** TERMINAL *****/

// Prints error message then exits the program
void die(const char* s) {
	write(STDOUT_FILENO, "\x1b[2J", 4); // Clears the screen
	write(STDOUT_FILENO, "\x1b[H", 3); // Reposition the cursor to row 1 collumn 1
	// Additional info about escape sequences can be found on the OUTPUT section below

	perror(s); // Prints the error message
	exit(1); // Exits the program with return value of 1 (failure)
}

// Disables raw input mode, will be called at program exit by atexit()
void disable_raw_mode(void) {
	/* Resets the terminal attrs to the original value
	 * If it fails, die() is called
	 */
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &ec.origTermios) == -1)
		die("disable_raw_mode()::tcsetattr()");
}

// Enables raw input mode, will be called at program start in main()
void enable_raw_mode(void) {
	/* Gets the terminal attrs and puts it into origTermios
	 * If it fails, die() is called
	 */
	if (tcgetattr(STDIN_FILENO, &ec.origTermios) == -1)
		die("enable_raw_mode()::tcgetattr()");

	atexit(disable_raw_mode); // Runs the disable_raw_mode() function at program exit

	struct termios raw = ec.origTermios; // Copy the original terminal attrs to raw

	/* Disable Ctrl-m, Ctrl-s, and Ctrl-q
	 * **********************************
	 * BRKINT controls break conditions such as Ctrl-c
	 * ICRNL controls Ctrl-m (carriage returns / new lines)
	 * INPCK controls parity checking (this is retro terminal thingy)
	 * ISTRIP controls stripping of the 8th bit of a input byte to 0, this should be turned off
	 * IXON controls Ctrl-s and Ctrl-q
	 */
	raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON); // input flags field

	/* Disables terminal output processing
	 * ***********************************
	 * OPOST controls post-processing of output
	 */
	raw.c_oflag &= ~(OPOST); // output flags field

	/* Sets the character size to 8 bits for one byte (1 byte = 8 bits)
	 * ****************************************************************
	 * CS8 is a bit mask which will be bitwise OR'd to set the character size to 8 bits per byte
	 */
	raw.c_cflag |= (CS8);

	/* Disable terminal echo, canonical mode, Ctrl-v, Ctrl-c, and Ctrl-z
	 * *****************************************************************
	 * ECHO controls terminal echo
	 * ICANON controls canonical mode
	 * IEXTEN controls Ctrl-v
	 * ISIG controls Ctrl-c and Ctrl-z
	 */
	raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG); // local / misc field

	// VMIN is an index in the c_cc field which sets the minimum number of bytes of input needed before read() returns
	raw.c_cc[VMIN] = 0; // read() can return with as little as 0 bytes of input

	// VTIME is an index in the c_cc field which sets the maximum amount of time to wait before read() returns
	raw.c_cc[VTIME] = 1; // Unit is milliseconds

	/* Sets the terminal attrs with the modified values inside raw
	 * If it fails, die() is called
	 */
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
		die("enable_raw_mode()::tcsetattr()");
}

/* Reads the input and catch errors
 * editor_read_key() is called by editor_process_keypress()
 */
int editor_read_key(void) {
	int nRead;
	char c;

	// Read input from stdin and puts it into c
	// If it fails, die() is called
	while ((nRead = read(STDIN_FILENO, &c, 1)) != 1) {
		if (nRead == -1 && errno != EAGAIN)
			die("editor_read_key()::read()");
	}

	if (c == '\x1b') {
		char seq[3];

		if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
		if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

		if (seq[0] == '[') {
			if (seq[1] >= '0' && seq[1] <= '9') {
				if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
				if (seq[2] == '~') {
					switch (seq[1]) {
						case '1':
							return HOME;
						case '3':
							return DEL;
						case '4':
							return END;
						case '5':
							return PAGE_UP;
						case '6':
							return PAGE_DOWN;
						case '7':
							return HOME;
						case '8':
							return END;
					}
				}
			} else {
				switch (seq[1]) {
					case 'A':
						return ARROW_UP;
					case 'B':
						return ARROW_DOWN;
					case 'C':
						return ARROW_RIGHT;
					case 'D':
						return ARROW_LEFT;
					case 'H':
						return HOME;
					case 'F':
						return END;
				}
			}
		} else if (seq[0] == 'O') {
			switch (seq[1]) {
				case 'H':
					return HOME;
				case 'F':
					return END;
			}
		}

		return '\x1b';
	} else {
		return c;
	}
}

// Gets the cursor position in the terminal
int get_cursor_position(int* rows, int* cols) {
	char buf[32];
	unsigned int i = 0;

	if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
		return -1;

	while (i < sizeof buf - 1) {
		if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
		if (buf[i] == 'R') break;
		i++;
	}

	buf[i] = '\0';

	if (buf[0] != '\x1b' || buf[1] != '[') return -1;
	if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;

	// IDK why this code below is still here :v
	// I don't think it's needed anymore btw
	printf("\r\n&buf[1]: '%s'\r\n", &buf[1]);

	return 0;
}

// Gets the window size of terminal
int get_window_size(int* rows, int* cols) {
	struct winsize ws;

	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
		if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) {
			return -1;
		}

		return get_cursor_position(rows, cols);
	} else {
		*rows = ws.ws_row;
		*cols = ws.ws_col;

		return 0;
	}
}

/***** ROW OPERATIONS *****/

void editor_append_row(char* s, size_t len) {
	ec.row = realloc(ec.row, sizeof(struct EditorRow) * (ec.numRows + 1));

	int at = ec.numRows;

	ec.row[at].size = len;
	ec.row[at].chars = malloc(len + 1);
	memcpy(ec.row[at].chars, s, len);
	ec.row[at].chars[len] = '\0';
	ec.numRows++;
}

/***** FILE IO *****/

void editor_open(char* filename) {
	FILE* fp = fopen(filename, "r");
	if (!fp) {
		die("editor_open()::fopen()");
	}

	char* line = NULL;
	size_t lineCap = 0;
	ssize_t lineLen;

	while ((lineLen = getline(&line, &lineCap, fp)) != -1) {
		while (lineLen > 0 && (line[lineLen - 1] == '\n' || line[lineLen - 1] == '\r')) {
			lineLen--;
		}

		editor_append_row(line, lineLen);
	}

	free(line);
	fclose(fp);
}

/***** APPEND BUFFER *****/

struct AppendBuffer {
	char* b;
	int len;
};

#define APPEND_BUFFER_INIT {NULL, 0}

void ab_append(struct AppendBuffer* ab, const char* s, int len) {
	char* new = realloc(ab->b, ab->len + len);

	if (new == NULL) return;
	memcpy(&new[ab->len], s, len);
	ab->b = new;
	ab->len += len;
}

void ab_free(struct AppendBuffer* ab) {
	free(ab->b);
}

/***** OUTPUT *****/

void editor_scroll(void) {
	if (ec.cury < ec.rowOffset) {
		ec.rowOffset = ec.cury;
	}

	if (ec.cury >= ec.rowOffset + ec.screenRows) {
		ec.rowOffset = ec.cury - ec.screenRows + 1;
	}
}

// Draws the tildes marking the lines / rows
void editor_draw_rows(struct AppendBuffer* ab) {
	for (int y = 0; y < ec.screenRows; y++) {
		int fileRow = y + ec.rowOffset;
		if (fileRow >= ec.numRows) {
			if (ec.numRows == 0 && y == ec.screenRows / 3) {
				char welcome[80];
				int welcomeLen = snprintf(welcome, sizeof welcome, "Seggs editor -- version %s", EDITOR_VERSION);

				if (welcomeLen > ec.screenCols) welcomeLen = ec.screenCols;

				int padding = (ec.screenCols - welcomeLen) / 2;
				if (padding) {
					ab_append(ab, "~", 1);
					padding--;
				}

				while (padding--) ab_append(ab, " ", 1);

				ab_append(ab, welcome, welcomeLen); // Appends the welcome message
			} else {
				ab_append(ab, "~", 1); // Append a tilde to buffer
			}
		} else {
			int len = ec.row[fileRow].size;
			if (len > ec.screenCols) {
				len = ec.screenCols;
			}

			ab_append(ab, ec.row[fileRow].chars, len);
		}

		ab_append(ab, "\x1b[K", 3); // Clears things to the right of cursor in current line

		if (y < ec.screenRows - 1) {
			ab_append(ab, "\r\n", 2); // Append carriage return and new line
		}
	}
}

// Refreshes the terminal screen
void editor_refresh_screen(void) {
	editor_scroll();

	struct AppendBuffer ab = APPEND_BUFFER_INIT;

	// Hide the cursor before drawing the tildes
	ab_append(&ab, "\x1b[?25l", 6);

	/* Write to stdout a VT100 escape sequence of \x1b[2J with size 4 bytes
	 * \x1b is the escape character, it is represented as 27 in decimal
	 * [2J means clear ('J') the entire screen (arg '2')
	 */

	// No longer used, replaced entire screen clearing with line clearing
	// ab_append(&ab, "\x1b[2J", 4);

	/* Write to stdout a VT100 escape sequence of \x1b[H with size 3 bytes
	 * [H means reposition the cursor ('H') at row 1 collumn 1 of the terminal
	 * H command defaults to row 1 collumn 1, but you can change it
	 * E.g. [69;420H means reposition cursor to row 69 collumn 420 of the terminal
	 * Row and collumn arguments are separated by ';' as you see there
	 * Row and collumn numbering starts from 1
	 */
	ab_append(&ab, "\x1b[H", 3);

	editor_draw_rows(&ab); // Draws the text editor rows

	char buf[32];
	snprintf(buf, sizeof buf, "\x1b[%d;%dH", (ec.cury - ec.rowOffset) + 1, ec.curx + 1);
	ab_append(&ab, buf, strlen(buf));

	// Show the cursor again after done drawing
	ab_append(&ab, "\x1b[?25h", 6);

	write(STDOUT_FILENO, ab.b, ab.len); // Do the screen clearing and cursor repositioning
	ab_free(&ab); // Free the dynamically allocated memory of ab->b (the buffer)
}

/***** INPUT *****/

// Handles cursor movement
void editor_move_cursor(int key) {
	switch (key) {
		case ARROW_LEFT:
			if (ec.curx != 0)
				ec.curx--;
			break;
		case ARROW_RIGHT:
			if (ec.curx != ec.screenCols - 1)
				ec.curx++;
			break;
		case ARROW_UP:
			if (ec.cury != 0)
				ec.cury--;
			break;
		case ARROW_DOWN:
			if (ec.cury < ec.numRows)
				ec.cury++;
			break;
	}
}

// Handles keypress input
void editor_process_keypress(void) {
	int c = editor_read_key();

	switch (c) {
		// If input is Ctrl-q, exit the program with return value of 0 (success)
		case CTRL_KEY('q'):
			write(STDOUT_FILENO, "\x1b[2J", 4); // Clears the screen
			write(STDOUT_FILENO, "\x1b[H", 3); // Reposition the cursor to row 1 collumn 1
			exit(0);
			break;

		case HOME:
			ec.curx = 0;
			break;
		case END:
			ec.curx = ec.screenCols - 1;
			break;

		case PAGE_UP:
		case PAGE_DOWN:
			{
				int times = ec.screenRows;
				while (times--) {
					editor_move_cursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
				}
			}
			break;

		case ARROW_LEFT:
		case ARROW_RIGHT:
		case ARROW_UP:
		case ARROW_DOWN:
			editor_move_cursor(c);
			break;
	}
}

/***** INIT *****/

// Acquire the terminal size
void init_editor(void) {
	// Initialize the cursor positions
	ec.curx = 0;
	ec.cury = 0;
	ec.rowOffset = 0;
	ec.numRows = 0;
	ec.row = NULL;

	// Gets the terminal rows and collumn size
	// If it fails, die() is called
	if (get_window_size(&ec.screenRows, &ec.screenCols) == -1)
		die("init_editor()::get_window_size()");
}

// Program starts here
int main(int argc, char* argv[argc + 1]) {
	enable_raw_mode(); // Enables raw mode in terminal

	init_editor(); // Gets the terminal size (initializing the screenRows and screenCols fields in ec)

	if (argc >= 2) {
		editor_open(argv[1]);
	}

	/* Refreshes the screen and runs the input gathering and processing function */
	while (1) {
		editor_refresh_screen();
		editor_process_keypress();
	}

	return 0;
}
