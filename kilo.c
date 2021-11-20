/***** README *****/

// This source file is categorized into "sections"
// Sections are marked with these headers
	/***** <SECTION_NAME> *****/

/***** INCLUDES *****/

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

/***** DEFINES *****/

#define CTRL_KEY(k) ((k) & 0x1f)

/***** DATA *****/

struct EditorConfig {
	struct termios origTermios; // Struct 'termios' named origTermios which contains fields defined in termios.h

	int screenRows;
	int screenCols;
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
char editor_read_key(void) {
	int nRead;
	char c;

	// Read input from stdin and puts it into c
	// If it fails, die() is called
	while ((nRead = read(STDIN_FILENO, &c, 1)) != 1) {
		if (nRead == -1 && errno != EAGAIN)
			die("editor_read_key()::read()");
	}

	return c;
}

// Gets the window size of terminal
int get_window_size(int* rows, int* cols) {
	struct winsize ws;

	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
		return -1;
	} else {
		*rows = ws.ws_row;
		*cols = ws.ws_col;

		return 0;
	}
}

/***** OUTPUT *****/

// Draws the tildes marking the lines / rows
void editor_draw_rows(void) {
	for (int y = 0; y < ec.screenRows; y++) {
		write(STDOUT_FILENO, "~\r\n", 3); // Write a tilde followed by carriage return and new line
	}
}

// Refreshes the terminal screen
void editor_refresh_screen(void) {
	/* Write to stdout a VT100 escape sequence of \x1b[2J with size 4 bytes
	 * \x1b is the escape character, it is represented as 27 in decimal
	 * [2J means clear ('J') the entire screen (arg '2')
	 */
	write(STDOUT_FILENO, "\x1b[2J", 4);

	/* Write to stdout a VT100 escape sequence of \x1b[H with size 3 bytes
	 * [H means reposition the cursor ('H') at row 1 collumn 1 of the terminal
	 * H command defaults to row 1 collumn 1, but you can change it
	 * E.g. [420;69H means reposition cursor to row 420 collumn 69 of the terminal
	 * Row and collumn arguments are separated by ';' as you see there
	 * Row and collumn numbering starts from 1
	 */
	write(STDOUT_FILENO, "\x1b[H", 3);

	editor_draw_rows(); // Draw tildes on new lines

	write(STDOUT_FILENO, "\x1b[H", 3); // Reposition the cursor again to the top left after drawing those tildes
}

/***** INPUT *****/

// Handles keypress input
void editor_process_keypress(void) {
	char c = editor_read_key();

	switch (c) {
		// If input is Ctrl-q, exit the program with return value of 0 (success)
		case CTRL_KEY('q'):
			write(STDOUT_FILENO, "\x1b[2J", 4); // Clears the screen
			write(STDOUT_FILENO, "\x1b[H", 3); // Reposition the cursor to row 1 collumn 1
			exit(0);
			break;
	}
}

/***** INIT *****/

// Acquire the terminal size
void init_editor(void) {
	// Gets the terminal rows and collumn size
	// If it fails, die() is called
	if (get_window_size(&ec.screenRows, &ec.screenCols) == -1)
		die("init_editor()::get_window_size()");
}
// Program starts here
int main() {
	enable_raw_mode(); // Enables raw mode in terminal

	init_editor(); // Gets the terminal size (initializing the screenRows and screenCols fields in ec)

	/* Refreshes the screen and runs the input gathering and processing function */
	while (1) {
		editor_refresh_screen();
		editor_process_keypress();
	}

	return 0;
}
