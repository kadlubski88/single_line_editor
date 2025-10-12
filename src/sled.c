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

#define VERSION "v0.2.0"
#define BUFFER_LENGTH 1024
#define OPTION_BYPASS 0x1
#define OPTION_PRINT 0x2
#define OPTION_OUTPUT_FILE 0x4
#define OPTION_NO_LINE_FEED 0x8
#define OPTION_FILE_NO_LINE_FEED 0x10
#define KEY_ESCAPE 27
#define KEY_OPEN_BRACKET 91
#define KEY_RIGHT 67
#define KEY_LEFT 68
#define KEY_DELETE 127

//######################
//# Function prototype #
//######################

char get_next_char(int stdin_fd);
int insert_char(char *text, char character, int target_index);
int remove_char(char *text, int target_index);
void redraw(FILE *data_stream, char *text, int cursor_index);
void early_exit(int return_code, struct termios *terminal_settings, const char *error_print, int stdin_fd);
void print_help(void);

//########
//# Main #
//########

int main(int argc, char *argv[]) {

    //###########################
    //# Variable initialisation #
    //###########################

    FILE *data_stream_in = stdin;
    FILE *data_stream_out = NULL;
    FILE *tty = NULL;
    int stdin_fd;
    
    struct termios term_settings_new, term_settings_old;
    struct pollfd pfd = { STDIN_FILENO, POLLIN, 0 };

    char text_buffer[BUFFER_LENGTH];
    size_t initial_buffer_length;
    char *output_file_name = NULL;
    char character = '\0';
    int cursor_index = 0;
    int option_flags = 0;

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

    if ((fgets(text_buffer, BUFFER_LENGTH, data_stream_in)) == NULL) {
        fprintf(stderr, "nothing to read\n");
        exit(EXIT_FAILURE);
    }

    //make sure there is no line feed at the end of the text
    initial_buffer_length = strlen(text_buffer);
    if (initial_buffer_length > 0 && text_buffer[initial_buffer_length - 1] == '\n') {
        text_buffer[initial_buffer_length - 1] = '\0';
    }

    //close data stream to file or pipe
    fclose(data_stream_in);
    

    //bypass the all edit text area
    if (option_flags & OPTION_BYPASS) {
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

    if ((tty = fopen("/dev/tty", "w")) == NULL) {
        fprintf(stderr, "not able to open the terminal\n");
        exit(EXIT_FAILURE);
    }
    //print the initial text
    fprintf(tty, "%s\r", text_buffer);
    fflush(tty);

    //editor loop
    while ((character = get_next_char(stdin_fd)) != '\n') {
        if (character == KEY_ESCAPE) { 
            //possible escape
            if (poll(&pfd, 1, 50) == 0) {
                fprintf(tty, "\r\033[K");
                early_exit(EXIT_SUCCESS, &term_settings_old, NULL, stdin_fd);
            }
            if (get_next_char(stdin_fd) != KEY_OPEN_BRACKET) {
                early_exit(EXIT_FAILURE, &term_settings_old, "wrong exit squence", stdin_fd);
            }
            switch (get_next_char(stdin_fd)) {
                case KEY_RIGHT:
                    if (text_buffer[cursor_index++] == '\0') {
                        cursor_index--;
                    }
                    fprintf(tty, "\033[%iG", cursor_index + 1);
                    break;
                case KEY_LEFT:
                    if (--cursor_index < 0) {
                        cursor_index++;
                    }
                    fprintf(tty, "\033[%iG", cursor_index + 1);
                    break;
                default:
                    //flush next character
                    get_next_char(stdin_fd);
                    continue;
                    break;
            } 
        } else if (character == KEY_DELETE) {
            if (--cursor_index < 0) {
                cursor_index++;
            } else {
                remove_char(text_buffer, cursor_index);
            }
            redraw(tty, text_buffer, cursor_index);
        } else {
            insert_char(text_buffer, character, cursor_index++);
            redraw(tty, text_buffer, cursor_index);
        }
        fflush(tty);
    }
    
    tcsetattr(stdin_fd, TCSANOW, &term_settings_old);
    fprintf(tty, "\r");
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
        if ((data_stream_out = fopen(output_file_name, "w")) == NULL) {
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

char get_next_char(int stdin_fd) {
    char buffer_char;
    read(stdin_fd, &buffer_char, 1);
    return(buffer_char);
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

void redraw(FILE *data_stream, char *text, int cursor_index) {
    fprintf(data_stream, "\r\033[K");
    fprintf(data_stream, "%s", text);
    fprintf(data_stream, "\033[%iG", cursor_index + 1);
    //fflush(data_stream);
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
    "  -b               Bypass editing and print directly\n"
    "  -p               Print the line to stdout\n"
    "  -n               Print without trailing newline\n"
    "  -f <file path>   Specify an alternative output file\n"
    "  -m               Write to file without trailing newline\n"
    "  -h               Print this help\n"
    "\nExample:\n"
    "  sled -p myfile.txt\n"
    );
}