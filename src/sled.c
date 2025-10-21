//###########################################################################
//# Single line editor (SLED) is a simple editor to edit a single line of   #
//# a file directly in the command line prompt.                             #
//# https://github.com/kadlubski88/single_line_editor                       #
//#                                                                         #
//# The MIT License (MIT)                                                   #
//# Copyright © 2025 Georges Kadlubski                                      #
//# URL: https://mit-license.org/                                           #
//###########################################################################

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <termios.h>
#include <poll.h>
#include <string.h>
#include <fcntl.h>

#define VERSION "v0.3.0"
#define BUFFER_LENGTH 1024
#define OPTION_BYPASS 0x1
#define OPTION_PRINT 0x2
#define OPTION_OUTPUT_FILE 0x4
#define OPTION_NO_LINE_FEED 0x8
#define OPTION_FILE_NO_LINE_FEED 0x10
#define OPTION_APPEND 0x20
#define TAB_SPACES 3

//##############################
//# Global variable definition #
//##############################

typedef enum{
    MODE_NAVI = 'N',
    MODE_EDIT = 'E',
    MODE_APPEND = 'A',
}edit_state;

typedef enum {
    KEY_NOKEY = 0,
    KEY_ESCAPE = 27,
    KEY_DELETE = 3,
    KEY_DOWN = 66,
    KEY_RIGHT = 67,
    KEY_LEFT = 68,
    KEY_BACKSPACE = 127,
    KEY_CHAR = 128,
    KEY_SPECIAL = '~',
    KEY_ENTER = '\n',
    KEY_TAB = '\t',
    KEY_UNKNOWN = -1
}key_type;

struct pollfd pfd = { STDIN_FILENO, POLLIN, 0 };
struct termios term_settings_new, term_settings_old;

//######################
//# Function prototype #
//######################

key_type get_next_key(int stdin_fd, char *value);
int insert_char(char *text, char character, int target_index);
int remove_char(char *text, int target_index);
void redraw(FILE *data_stream, char mode_char, char *text, int cursor_index);
void early_exit(int return_code, struct termios *terminal_settings, const char *error_print, int stdin_fd);
void print_help(void);

//########
//# Main #
//########

int main(int argc, char *argv[]) {

    //#######################
    //# Variable definition #
    //#######################

    FILE *data_stream_in = stdin;
    FILE *data_stream_out = NULL;
    FILE *tty = NULL;
    int stdin_fd;
    char text_buffer[BUFFER_LENGTH];

    key_type next_key = KEY_UNKNOWN;
    char next_value = '\0';
    edit_state actual_mode = MODE_NAVI;
    
    int cursor_index = 0;
    int option_flags = 0;

    char output_file_open_mode[] = "w";
    char *output_file_name = NULL;

    //####################
    //# Argument parsing #
    //####################

    argc--;
    argv++;
    while (argc > 0 && argv[0][0] == '-' ) {
        switch (argv[0][1]) {
        //help option
        case 'h':
            print_help();
            exit(EXIT_SUCCESS);
        //print version
        case 'v':
            printf("%s\n", VERSION);
            exit(EXIT_SUCCESS);
        //append option
        case 'a':
            option_flags |= OPTION_APPEND;
            output_file_open_mode[0] = 'a';
            argc--;
            break;    
        //bypass option
        case 'b':
            option_flags |= (OPTION_BYPASS | OPTION_PRINT);
            argc--;
            break;
        //print option
        case 'p':
            option_flags |= OPTION_PRINT;
            argc--;
            break;
        //write to stdout without line feed option
        case 'n':
            option_flags |= OPTION_NO_LINE_FEED;
            argc--;
            break;
        //write to stdout without line feed option
        case 'm':
            option_flags |= OPTION_FILE_NO_LINE_FEED;
            argc--;
            break;
        //alternative output file option
        case 'f':
            argv++;
            option_flags |= OPTION_OUTPUT_FILE;
            output_file_name = *argv;
            argc -= 2;
            break;
        default:
            fprintf(stderr, "\"%s\" is not a valid option\n", *argv);
            exit(EXIT_FAILURE);
            break;
        }
        argv++;
    }
    //source file (and destination file if -f not setted)
    if (argc > 0) {
        if ((data_stream_in = fopen(*argv, "r")) == NULL) {
            fprintf(stderr, "file \"%s\" doesn\'t exists\n", *argv);
            exit(EXIT_FAILURE);
        }
        option_flags |= OPTION_OUTPUT_FILE;
        if (!output_file_name) {
            output_file_name = *argv;
        }
    }

    //read first line of file or pipe
    if ((fgets(text_buffer, BUFFER_LENGTH, data_stream_in)) == NULL) {
        fprintf(stderr, "nothing to read\n");
        exit(EXIT_FAILURE);
    }
    if (strlen(text_buffer) > 0 && text_buffer[strlen(text_buffer) - 1] == '\n') {
        text_buffer[strlen(text_buffer) - 1] = '\0';
    }

    //close pipe
    if (data_stream_in == stdin) {
        fclose(data_stream_in);
    }
    
    //bypass the all edit text area
    if (option_flags & OPTION_BYPASS) {
        fclose(data_stream_in);
        goto bypass_edit;
    }

    //#############
    //# Edit text #
    //#############

    //open new file descriptor to read user entry
    stdin_fd = open("/dev/tty", O_RDONLY);

    //set terminal to non canonical STDIN_FILENO
    tcgetattr(stdin_fd, &term_settings_old);
    term_settings_new = term_settings_old;
    term_settings_new.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(stdin_fd, TCSANOW, &term_settings_new);

    //open terminal to write
    if ((tty = fopen("/dev/tty", "w")) == NULL) {
        fprintf(stderr, "not able to open the terminal\n");
        exit(EXIT_FAILURE);
    }

    //print the initial text
    redraw(tty, actual_mode, text_buffer, cursor_index);

    //editor loop
    while ((next_key = get_next_key(stdin_fd, &next_value)) != KEY_ENTER) {
        if (next_key == KEY_ESCAPE) {
            fprintf(tty, "\r\033[K");
            early_exit(EXIT_SUCCESS, &term_settings_old, NULL, stdin_fd);
        }
        switch (next_key) {
            case KEY_DOWN:
                if (actual_mode == MODE_NAVI) {
                    if ((fgets(text_buffer, BUFFER_LENGTH, data_stream_in)) == NULL) {
                        actual_mode = MODE_APPEND;
                        option_flags |= OPTION_APPEND;
                        output_file_open_mode[0] = 'a';
                    }
                    if (strlen(text_buffer) > 0 && text_buffer[strlen(text_buffer) - 1] == '\n') {
                        text_buffer[strlen(text_buffer) - 1] = '\0';
                    }
                }
                break;
            case KEY_LEFT:
                actual_mode = option_flags & OPTION_APPEND ? MODE_APPEND : MODE_EDIT;
                if (--cursor_index < 0) {
                    cursor_index++;
                }
                break;
            case KEY_RIGHT:
                actual_mode = option_flags & OPTION_APPEND ? MODE_APPEND : MODE_EDIT;
                if (text_buffer[cursor_index++] == '\0') {
                    cursor_index--;
                }
                break;
            case KEY_BACKSPACE:
                actual_mode = option_flags & OPTION_APPEND ? MODE_APPEND : MODE_EDIT;
                if (--cursor_index < 0) {
                    cursor_index++;
                } else {
                    remove_char(text_buffer, cursor_index);
                }
                break;  
            case KEY_CHAR:
                actual_mode = option_flags & OPTION_APPEND ? MODE_APPEND : MODE_EDIT;
                insert_char(text_buffer, next_value, cursor_index++);
                break;
            default:
                break;
        }
        redraw(tty, actual_mode, text_buffer, cursor_index);
    }
    
    tcsetattr(stdin_fd, TCSANOW, &term_settings_old);
    fprintf(tty, "\r\033[K");
    fflush(tty);

    //editor loop end
    bypass_edit:
    
    //###############
    //# Output text #
    //###############

    if (option_flags & OPTION_PRINT) {
        fprintf(stdout, "%s%s", text_buffer, (option_flags & OPTION_NO_LINE_FEED) ? "" : "\n");
        fflush(stdout);
    }

    if (option_flags & OPTION_OUTPUT_FILE) {
        if ((data_stream_out = fopen(output_file_name, output_file_open_mode)) == NULL) {
            fprintf(stderr, "not able to open the destination file %s\n", output_file_name);
            exit(EXIT_FAILURE);
        }
        fprintf(data_stream_out, "%s%s", text_buffer, (option_flags & OPTION_FILE_NO_LINE_FEED) ? "" : "\n");
        fclose(data_stream_out);
    }
    exit(EXIT_SUCCESS);
}

//#######################
//# Function definition #
//#######################

key_type get_next_key(int stdin_fd, char *value) {
    char buffer_char;
    int last_key = KEY_UNKNOWN;
    int escape_sequence = 0;
    while (read(stdin_fd, &buffer_char, 1) > 0) {
        *value = buffer_char;
        //Escape key
        if (buffer_char == KEY_ESCAPE) {
            if (poll(&pfd, 1, 20) == 0) {
                return(KEY_ESCAPE);
            }
            escape_sequence = 1;
            continue;
        }
        //ANSI escape codes
        if (escape_sequence) {
            if (buffer_char == '[') {
                continue;
            }
            switch ((int)buffer_char) {
                case KEY_DELETE:
                    if (poll(&pfd, 1, 20) == 0) {
                        return(KEY_UNKNOWN);
                    }
                    last_key = (int)buffer_char;
                    continue;
                case KEY_SPECIAL:
                    return(last_key);
                case KEY_DOWN:
                case KEY_LEFT:
                case KEY_RIGHT:
                    return((int)buffer_char);
                default:
                    return(KEY_UNKNOWN);
            }
        }
        //ASCII printable characters
        if (buffer_char >= ' ' && buffer_char <= '~') {
            return(KEY_CHAR);
        }
        //ASCII unprintable control codes
        switch ((int)buffer_char) {
            case KEY_ENTER:
            case KEY_BACKSPACE:
                return((int)buffer_char);
            case KEY_TAB:
                return(KEY_CHAR);
            default:
                return(KEY_UNKNOWN);
        }
    }
    return(KEY_UNKNOWN);
}

int insert_char(char *text, char character, int target_index) {
    int index = 0;
    char char_buffer = '\0';
    while (*text != '\0') {
        if (index++ >= target_index) {
            char_buffer = *text;
            *text = character;
            character = char_buffer;
        }
        text++;
    }
    *text++ = character;
    *text = '\0';
    return 0;
}

int remove_char(char *text, int target_index) {
    int index = 0;
    while (*text != '\0') {
        if (index++ >= target_index) {
            *text = *(text + 1);
        }
        text++;
    }
    return 0;
}

void redraw(FILE *data_stream, char mode_char, char *text, int cursor_index) {
    char drawing_buffer[BUFFER_LENGTH];
    int index_offset = 0;
    int cursor_offset = 0;
    int i = 0;
    for (i; text[i] != '\0'; i++) {
        if (text[i] == '\n') {
            continue;
        }
        if (text[i] == '\t') {
            for (int j = 0; j < TAB_SPACES; j++) {
                drawing_buffer[i + index_offset] = ' ';
                index_offset++;
                cursor_offset += (int) (i < cursor_index);
            }
            index_offset--;
            cursor_offset -= (int) (i < cursor_index);
        } else {
            drawing_buffer[i + index_offset] = text[i];
        }
    }
    drawing_buffer[i + index_offset] = '\0';
    fprintf(data_stream, "\r\033[K");
    fprintf(data_stream, "[%c]%s",mode_char, drawing_buffer);
    fprintf(data_stream, "\033[%iG", cursor_index + 1 + 3 + cursor_offset);
    fflush(data_stream);
    return;
}

void early_exit(int return_code, struct termios *terminal_settings, const char *error_print, int stdin_fd) {
    tcsetattr(stdin_fd, TCSANOW, terminal_settings);
    if (error_print) {
        fprintf(stderr, "%s\n", error_print);
        fflush(stderr);
    }
    exit(return_code);
}

void print_help(void) {
    printf("Single Line Editor (SLED) %s\n\n", VERSION);
    printf(
    "Usage:\n"
    "  sled [options] [file]\n\n"
    "Options:\n"
    "  -a               Append text to the end of the file\n"
    "  -b               Bypass editing and print directly\n"
    "  -p               Print the line to stdout\n"
    "  -n               Print without trailing newline\n"
    "  -f <file path>   Specify an alternative output file\n"
    "  -m               Write to file without trailing newline\n"
    "  -v               Print the version\n"
    "  -h               Print this help\n"
    "\nExample:\n"
    "  sled -p myfile.txt\n"
    );
}