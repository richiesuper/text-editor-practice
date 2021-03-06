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
#include <fcntl.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/***** DEFINES *****/

#define EDITOR_NAME "TED - Text EDit"
#define EDITOR_AUTHOR "Richie Seputro"
#define EDITOR_VERSION "4.20.69"
#define EDITOR_QUIT_TIMES 3
#define STATUS_DURATION 8
#define EDITOR_TAB_STOP 8
#define CTRL_KEY(k) ((k) & 0x1f)

enum EditorKey {
	BACKSPACE = 127,
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

	int rsize;
	char* render;
};

struct EditorConfig {
	struct termios origTermios; // Struct 'termios' named origTermios which contains fields defined in termios.h

	int screenRows; // Number of terminal rows
	int screenCols; // Number of terminal collumns

	int curx; // Cursor x position
	int cury; // Cursor y position

	int rx; // struct EditorRow's render x coordinate

	int rowOffset;
	int colOffset;

	int numRows;
	struct EditorRow* row;

	int modified;

	char* filename;

	char statusmsg[80];
	time_t statusmsg_time;
} ec;

/***** FUNCTION PROTOTYPES *****/

void editor_set_status_message(const char* fmt, ...);
void editor_refresh_screen(void);
char* editor_prompt(char* prompt, void (*callback)(char*, int));

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

int editor_row_curx_to_rx(struct EditorRow* row, int curx) {
	int rx = 0;
	for (int j = 0; j < curx; j++) {
		if (row->chars[j] == '\t') {
			rx += (EDITOR_TAB_STOP - 1) - (rx % EDITOR_TAB_STOP);
		}

		rx++;
	}

	return rx;
}

int editor_row_rx_to_curx(struct EditorRow* row, int rx) {
	int curRx = 0;
	int curx;
	for (curx = 0; curx < row->size; curx++) {
		if (row->chars[curx] == '\t') {
			curRx += (EDITOR_TAB_STOP - 1) - (curRx % EDITOR_TAB_STOP);
		}
		curRx++;

		if (curRx > rx) {
			return curx;
		}
	}

	return curx;
}

void editor_update_row(struct EditorRow* row) {
	int tabs = 0;
	for (int j = 0; j < row->size; j++) {
		if (row->chars[j] == '\t') {
			tabs++;
		}
	}

	free(row->render);
	row->render = malloc(row->size + tabs * (EDITOR_TAB_STOP - 1) + 1);

	int idx = 0;
	for (int j = 0; j < row->size; j++) {
		if (row->chars[j] == '\t') {
			row->render[idx] = ' ';
			idx++;

			while (idx % EDITOR_TAB_STOP) {
				row->render[idx] = ' ';
				idx++;
			}
		} else {
			row->render[idx] = row->chars[j];
			idx++;
		}
	}

	row->render[idx] = '\0';
	row->rsize = idx;
}

void editor_insert_row(int at, char* s, size_t len) {
	if (at < 0 || at > ec.numRows) {
		return;
	}

	ec.row = realloc(ec.row, sizeof(struct EditorRow) * (ec.numRows + 1));
	memmove(&ec.row[at + 1], &ec.row[at], sizeof(struct EditorRow) * (ec.numRows - at));

	ec.row[at].size = len;
	ec.row[at].chars = malloc(len + 1);
	memcpy(ec.row[at].chars, s, len);
	ec.row[at].chars[len] = '\0';

	ec.row[at].rsize = 0;
	ec.row[at].render = NULL;

	editor_update_row(&ec.row[at]);

	ec.numRows++;
	ec.modified++;
}

void editor_free_row(struct EditorRow* row) {
	free(row->render);
	free(row->chars);
}

void editor_del_row(int at) {
	if (at < 0 || at >= ec.numRows) {
		return;
	}

	editor_free_row(&ec.row[at]);
	memmove(&ec.row[at], &ec.row[at + 1], sizeof(struct EditorRow) * (ec.numRows - at - 1));
	ec.numRows--;
	ec.modified++;
}

void editor_row_insert_char(struct EditorRow* row, int at, int c) {
	if (at < 0 || at > row->size) {
		at = row->size;
	}

	row->chars = realloc(row->chars, row->size + 2);
	memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
	row->size++;
	row->chars[at] = c;
	editor_update_row(row);
}

void editor_row_append_string(struct EditorRow* row, char* s, size_t len) {
	row->chars = realloc(row->chars, row->size + len + 1);
	memcpy(&row->chars[row->size], s, len);
	row->size += len;
	row->chars[row->size] = '\0';
	editor_update_row(row);
	ec.modified++;
}

void editor_row_del_char(struct EditorRow* row, int at) {
	if (at < 0 || at >= row->size) {
		return;
	}

	memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
	row->size--;
	editor_update_row(row);
	ec.modified++;
}

/***** EDITOR OPERATIONS *****/

void editor_insert_char(int c) {
	if (ec.cury == ec.numRows) {
		editor_insert_row(ec.numRows, "", 0);
	}

	editor_row_insert_char(&ec.row[ec.cury], ec.curx, c);
	ec.curx++;
	ec.modified++;
}

void editor_insert_newline(void) {
	if (ec.curx == 0) {
		editor_insert_row(ec.cury, "", 0);
	} else {
		struct EditorRow* row = &ec.row[ec.cury];
		editor_insert_row(ec.cury + 1, &row->chars[ec.curx], row->size - ec.curx);
		row = &ec.row[ec.cury];
		row->size = ec.curx;
		row->chars[row->size] = '\0';
		editor_update_row(row);
	}

	ec.cury++;
	ec.curx = 0;
}

void editor_del_char(void) {
	if (ec.cury == ec.numRows) {
		return;
	}

	if (ec.curx == 0 && ec.cury == 0) {
		return;
	}

	struct EditorRow* row = &ec.row[ec.cury];
	if (ec.curx > 0) {
		editor_row_del_char(row, ec.curx - 1);
		ec.curx--;
	} else {
		ec.curx = ec.row[ec.cury - 1].size;
		editor_row_append_string(&ec.row[ec.cury - 1], row->chars, row->size);
		editor_del_row(ec.cury);
		ec.cury--;
	}
}

/***** FILE IO *****/

char* editor_rows_to_string(int* buflen) {
	int totlen = 0;
	for (int j = 0; j < ec.numRows; j++) {
		totlen += ec.row[j].size + 1;
	}

	*buflen = totlen;

	char* buf = malloc(totlen);
	char* p = buf;

	for (int j = 0; j < ec.numRows; j++) {
		memcpy(p, ec.row[j].chars, ec.row[j].size);
		p += ec.row[j].size;
		*p = '\n';
		p++;
	}

	return buf;
}

void editor_open(char* filename) {
	free(ec.filename);
	ec.filename = strdup(filename);

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

		editor_insert_row(ec.numRows, line, lineLen);
	}

	free(line);
	fclose(fp);
	ec.modified = 0;
}

void editor_save(void) {
	if (ec.filename == NULL) {
		ec.filename = editor_prompt("Save as: %s (ESC to cancel)", NULL);
		if (ec.filename == NULL) {
			editor_set_status_message("Save aborted");
			return;
		}
	}

	int len;
	char* buf = editor_rows_to_string(&len);

	int fd = open(ec.filename, O_RDWR | O_CREAT, 0644);
	if (fd != 1) {
		if (ftruncate(fd, len) != -1) {
			if (write(fd, buf, len) == len) {
				close(fd);
				free(buf);
				ec.modified = 0;
				editor_set_status_message("%s: %d bytes written to disk", ec.filename, len);
				return;
			}
		}

		close(fd);
	}

	free(buf);
	editor_set_status_message("%s: save failed! I/O error: %s", strerror(errno));
}

/***** FIND *****/

void editor_find_callback(char* query, int key) {
	/* lastMatch will store the index of the last match is such match existed, or -1 if no such match existed */
	static int lastMatch = -1;

	/* direction will store the value 1 if it's forward and -1 if it's backward */
	static int direction = 1;

	if (key == '\r' || key == '\x1b') {
		/* Restore lastMatch and direction's initial value */
		lastMatch = -1;
		direction = 1;
		return;
	} else if (key == ARROW_RIGHT || key == ARROW_DOWN) {
		direction = 1;
	} else if (key == ARROW_LEFT || key == ARROW_RIGHT) {
		direction = -1;
	} else {
		lastMatch = -1;
		direction = 1;
	}

	if (lastMatch == -1) {
		direction = 1;
	}

	int current = lastMatch;
	for (int i = 0; i < ec.numRows; i++) {
		current += direction;

		/* Search wrapping logic */
		if (current == -1) {
			current = ec.numRows - 1;
		} else if (current == ec.numRows) {
			current = 0;
		}

		struct EditorRow* row = &ec.row[current];
		char* match = strstr(row->render, query);
		if (match) {
			lastMatch = current;
			ec.cury = current;
			ec.curx = editor_row_rx_to_curx(row, match - row->render);
			ec.rowOffset = ec.numRows;
			break;
		}
	}
}

void editor_find(void) {
	int savedCurx = ec.curx;
	int savedCury = ec.cury;
	int savedColOffset = ec.colOffset;
	int savedRowOffset = ec.rowOffset;

	char* query = editor_prompt("Search: %s (Use ESC/Arrows/Enter)", editor_find_callback);

	if (query) {
		free(query);
	} else {
		ec.curx = savedCurx;
		ec.cury = savedCury;
		ec.colOffset = savedColOffset;
		ec.rowOffset = savedRowOffset;
	}
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
	ec.rx = 0;

	if (ec.cury < ec.numRows) {
		ec.rx = editor_row_curx_to_rx(&ec.row[ec.cury], ec.curx);
	}

	if (ec.cury < ec.rowOffset) {
		ec.rowOffset = ec.cury;
	}

	if (ec.cury >= ec.rowOffset + ec.screenRows) {
		ec.rowOffset = ec.cury - ec.screenRows + 1;
	}

	if (ec.rx < ec.colOffset) {
		ec.colOffset = ec.rx;
	}

	if (ec.rx >= ec.colOffset + ec.screenCols) {
		ec.colOffset = ec.rx - ec.screenCols + 1;
	}
}

// Draws the tildes marking the lines / rows
void editor_draw_rows(struct AppendBuffer* ab) {
	for (int y = 0; y < ec.screenRows; y++) {
		int fileRow = y + ec.rowOffset;
		if (fileRow >= ec.numRows) {
			if (ec.numRows == 0 && y == ec.screenRows / 3) {
				char welcome[80];

				int welcomeLen = snprintf(welcome, sizeof welcome, "%s -- version %s", EDITOR_NAME, EDITOR_VERSION);

				if (welcomeLen > ec.screenCols) welcomeLen = ec.screenCols;

				int wpadding = (ec.screenCols - welcomeLen) / 2;

				if (wpadding) {
					ab_append(ab, "~", 1);
					wpadding--;
				}
				while (wpadding--) ab_append(ab, " ", 1);

				ab_append(ab, welcome, welcomeLen); // Appends the welcome message
			} else if (ec.numRows == 0 && y == (ec.screenRows / 3) + 2) {
				char author[80];

				int authorLen = snprintf(author, sizeof author, "Made by %s", EDITOR_AUTHOR);

				if (authorLen > ec.screenCols) authorLen = ec.screenCols;

				int apadding = (ec.screenCols - authorLen) / 2;

				if (apadding) {
					ab_append(ab, "~", 1);
					apadding--;
				}
				while (apadding--) ab_append(ab, " ", 1);

				ab_append(ab, author, authorLen); // Appends the author message
			} else {
				ab_append(ab, "~", 1); // Append a tilde to buffer
			}
		} else {
			int len = ec.row[fileRow].rsize - ec.colOffset;

			if (len < 0) {
				len = 0;
			}

			if (len > ec.screenCols) {
				len = ec.screenCols;
			}

			ab_append(ab, &ec.row[fileRow].render[ec.colOffset], len);
		}

		ab_append(ab, "\x1b[K", 3); // Clears things to the right of cursor in current line
		ab_append(ab, "\r\n", 2); // Append carriage return and new line
	}
}

void editor_draw_status_bar(struct AppendBuffer* ab) {
	ab_append(ab, "\x1b[7m", 4);

	char status[80];
	char rstatus[80];

	int len = snprintf(status, sizeof status, "%.20s - %d lines %s", ec.filename ? ec.filename : "[No Name]", ec.numRows, ec.modified ? "(modified)" : "");
	int rlen = snprintf(rstatus, sizeof rstatus, "%d/%d", ec.cury + 1, ec.numRows);

	if (len > ec.screenCols) {
		len = ec.screenCols;
	}
	ab_append(ab, status, len);

	while (len < ec.screenCols) {
		if (ec.screenCols - len == rlen) {
			ab_append(ab, rstatus, rlen);
			break;
		} else {
			ab_append(ab, " ", 1);
			len++;
		}
	}

	ab_append(ab, "\x1b[m", 3);
	ab_append(ab, "\r\n", 2);
}

void editor_draw_message_bar(struct AppendBuffer* ab) {
	ab_append(ab, "\x1b[K", 3);

	int msglen = strlen(ec.statusmsg);
	if (msglen > ec.screenCols) {
		msglen = ec.screenCols;
	}

	if (msglen && time(NULL) - ec.statusmsg_time < STATUS_DURATION) {
		ab_append(ab, ec.statusmsg, msglen);
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

	editor_draw_status_bar(&ab); // Draws the text editor status bar

	editor_draw_message_bar(&ab); // Draws the text editor status message

	char buf[32];
	snprintf(buf, sizeof buf, "\x1b[%d;%dH", (ec.cury - ec.rowOffset) + 1, (ec.rx - ec.colOffset) + 1);
	ab_append(&ab, buf, strlen(buf));

	// Show the cursor again after done drawing
	ab_append(&ab, "\x1b[?25h", 6);

	write(STDOUT_FILENO, ab.b, ab.len); // Do the screen clearing and cursor repositioning
	ab_free(&ab); // Free the dynamically allocated memory of ab->b (the buffer)
}

void editor_set_status_message(const char* fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(ec.statusmsg, sizeof ec.statusmsg, fmt, ap);
	va_end(ap);

	ec.statusmsg_time = time(NULL);
}

/***** INPUT *****/

char* editor_prompt(char* prompt, void (*callback)(char*, int)) {
	size_t bufsize = 128;
	char* buf = malloc(bufsize);

	size_t buflen = 0;
	buf[0] = '\0';

	while (1) {
		editor_set_status_message(prompt, buf);
		editor_refresh_screen();

		int c = editor_read_key();
		if (c == DEL || c == CTRL_KEY('h') || c == BACKSPACE) {
			if (buflen != 0) {
				buf[--buflen] = '\0';
			}
		} else if (c == '\x1b') {
			editor_set_status_message("");
			if (callback) {
				callback(buf, c);
			}

			free(buf);
			return NULL;
		} else if (c == '\r') {
			if (buflen != 0) {
				editor_set_status_message("");
				if (callback) {
					callback(buf, c);
				}

				return buf;
			}
		} else if (!iscntrl(c) && c < 128) {
			if (buflen == bufsize - 1) {
				bufsize *= 2;
				buf = realloc(buf, bufsize);
			}
			buf[buflen++] = c;
			buf[buflen] = '\0';
		}

		if (callback) {
			callback(buf, c);
		}
	}
}

// Handles cursor movement
void editor_move_cursor(int key) {
	struct EditorRow* row = (ec.cury >= ec.numRows) ? NULL : &ec.row[ec.cury];

	switch (key) {
		case ARROW_LEFT:
			if (ec.curx != 0) {
				ec.curx--;
			} else if (ec.cury > 0) {
				ec.cury--;
				ec.curx = ec.row[ec.cury].size;
			}
			break;
		case ARROW_RIGHT:
			if (row && ec.curx < row->size) {
				ec.curx++;
			} else if (row && ec.curx == row->size) {
				ec.cury++;
				ec.curx = 0;
			}
			break;
		case ARROW_UP:
			if (ec.cury != 0) {
				ec.cury--;
			}
			break;
		case ARROW_DOWN:
			if (ec.cury < ec.numRows) {
				ec.cury++;
			}
			break;
	}

	row = (ec.cury >= ec.numRows) ? NULL : &ec.row[ec.cury];
	int rowLen = row ? row->size : 0;
	if (ec.curx > rowLen) {
		ec.curx = rowLen;
	}
}

// Handles keypress input
void editor_process_keypress(void) {
	static int quitTimes = EDITOR_QUIT_TIMES;
	int c = editor_read_key();

	switch (c) {
		case '\r':
			editor_insert_newline();
			break;

		// If input is Ctrl-q, exit the program with return value of 0 (success)
		case CTRL_KEY('q'):
			if (ec.modified && quitTimes > 0) {
				editor_set_status_message("File has unsaved changes! Press Ctrl-q %d more times to quit.", quitTimes);
				quitTimes--;
				return;
			}

			write(STDOUT_FILENO, "\x1b[2J", 4); // Clears the screen
			write(STDOUT_FILENO, "\x1b[H", 3); // Reposition the cursor to row 1 collumn 1
			exit(0);
			break;

		case CTRL_KEY('s'):
			editor_save();
			break;

		case HOME:
			ec.curx = 0;
			break;
		case END:
			if (ec.cury < ec.numRows) {
				ec.curx = ec.row[ec.cury].size;
			}
			break;

		case CTRL_KEY('f'):
			editor_find();
			break;

		case BACKSPACE:
		case CTRL_KEY('h'):
		case DEL:
			if (c == DEL) {
				editor_move_cursor(ARROW_RIGHT);
			}
			editor_del_char();

			break;

		case PAGE_UP:
		case PAGE_DOWN:
			{
				if (c == PAGE_UP) {
					ec.cury = ec.rowOffset;
				} else if (c == PAGE_DOWN) {
					ec.cury = ec.rowOffset + ec.screenRows - 1;

					if (ec.cury > ec.numRows) {
						ec.cury = ec.numRows;
					}
				}

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

		case CTRL_KEY('l'):
		case '\x1b':
			break;

		default:
			editor_insert_char(c);
			break;
	}

	quitTimes = EDITOR_QUIT_TIMES;
}

/***** INIT *****/

// Acquire the terminal size
void init_editor(void) {
	// Initialize the cursor positions
	ec.curx = 0;
	ec.cury = 0;
	ec.rx = 0;
	ec.rowOffset = 0;
	ec.colOffset = 0;
	ec.numRows = 0;
	ec.row = NULL;
	ec.modified = 0;
	ec.filename = NULL;
	ec.statusmsg[0] = '\0';
	ec.statusmsg_time = 0;

	// Gets the terminal rows and collumn size
	// If it fails, die() is called
	if (get_window_size(&ec.screenRows, &ec.screenCols) == -1)
		die("init_editor()::get_window_size()");

	ec.screenRows -= 2;
}

// Program starts here
int main(int argc, char* argv[argc + 1]) {
	enable_raw_mode(); // Enables raw mode in terminal

	init_editor(); // Gets the terminal size (initializing the screenRows and screenCols fields in ec)

	if (argc >= 2) {
		editor_open(argv[1]);
	}

	editor_set_status_message("Ctrl-s: save | Ctrl-q: quit | Ctrl-f = find");

	/* Refreshes the screen and runs the input gathering and processing function */
	while (1) {
		editor_refresh_screen();
		editor_process_keypress();
	}

	return 0;
}
