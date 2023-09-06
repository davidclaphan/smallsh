#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <stddef.h>
#include <signal.h>

// tilde expansion
char *str_gsub(char *restrict *restrict haystack, char const *restrict needle, char const *restrict sub)
{
  char *str = *haystack;
  size_t haystack_len = strlen(str);
  size_t const needle_len = strlen(needle), sub_len = strlen(sub);

  for (; (str = strstr(str, needle));) {
    ptrdiff_t off = str - *haystack;
    if (sub_len > needle_len) {
      str = realloc(*haystack, sizeof **haystack * (haystack_len + sub_len - needle_len + 1));
      if (!str) goto exit;
      *haystack = str;
      str = *haystack + off;
    }

    memmove(str + sub_len, str + needle_len, haystack_len + 1 - off - needle_len);
    memcpy(str, sub, sub_len);
    haystack_len = haystack_len + sub_len - needle_len;
    str += sub_len;
    if(strcmp(needle, "~") == 0) break;
  }
  str = *haystack;
  if (sub_len < needle_len) {
    str = realloc(*haystack, sizeof **haystack * (haystack_len + 1));
    if (!str) goto exit;
    *haystack = str;
  }

exit:
  return str;
}
    
struct
input_struct {
  char *command; 
  char *arguments[512];
  char *infile;
  char *outfile;
  bool background;
};
struct sigaction ignore_action = {0};
struct sigaction do_nothing_action = {0};
ssize_t char_read;
char *inputLine;
size_t len;

void handle_SIGINT(int signo) {}

int main(void) {

  char *IFS = getenv("IFS");
  if (IFS == NULL) {
    IFS = " \t\n";
  }

  char *PS1 = getenv("PS1");
  if (PS1 == NULL) {
    PS1 = "";
  }

  char *HOME = getenv("HOME"); 
  if (HOME == NULL) {
    HOME = "";
  }  

  pid_t PID = getpid();
  char *p_pid = malloc(8);
  sprintf(p_pid, "%d", PID);

  int last_foreground = 0;
  int last_background = 0;

  pid_t childPID;
  int background_status_check;

  // signal handling
  ignore_action.sa_handler = SIG_IGN;
  do_nothing_action.sa_handler = handle_SIGINT;
  sigaction(SIGTSTP, &ignore_action, NULL);
  sigaction(SIGINT, &ignore_action, NULL);

  while (1) {
    
    // background process check
    while ((childPID = waitpid(0, &background_status_check, WNOHANG | WUNTRACED)) > 0) {

      // waitpid() returns process ID of child whose state is changed
      if (childPID > 0) {

        if (WIFEXITED(background_status_check)) {
          int exitStatus = WEXITSTATUS(background_status_check);
          fprintf(stderr, "Child process %jd done. Exit status %d.\n", (intmax_t) childPID, exitStatus);
        }

        else if (WIFSIGNALED(background_status_check)) {
          int exitSignal = WTERMSIG(background_status_check);
          fprintf(stderr, "Child process %jd. Signaled %d.\n", (intmax_t) childPID, exitSignal);
        }

        else if (WIFSTOPPED(background_status_check)) {
          kill(childPID, SIGCONT);
          fprintf(stderr, "Child process %jd stopped. Continuing.\n", (intmax_t) childPID);
        }
      } 
    }
      
    // foreground process conversion for $? expansion
    char *p_last_foreground = malloc(8);
    sprintf(p_last_foreground, "%d", last_foreground);

    // background process conversion for $! expansion
    char *p_last_background = malloc(8);
    sprintf(p_last_background, "%d", last_background);
    if (strcmp(p_last_background, "0") == 0) {
      p_last_background = "";
    }

    // input/output file flags
    struct input_struct command_line = {NULL, {NULL}, NULL, NULL, 0};
    int input_file_flag = 0;
    int output_file_flag = 0;

    fprintf(stderr, PS1);

    //get COMMANDS (INPUT)
    sigaction(SIGINT, &do_nothing_action, &ignore_action);
    char_read = getline(&inputLine, &len, stdin);

    // EOF handling
    if (feof(stdin)) {
      fprintf(stderr, "\nexit\n");
      exit(last_foreground);
    }

    // split words to tokens
    int counter = 0;
    char* token = strtok(inputLine, IFS);
    
    // error handling for NULL token
    if (token == NULL) {goto exit;}

    // ASSIGNMENT of COMMAND
    command_line.command = strdup(token);

    // EXPANSION of COMMAND
    if (strncmp(command_line.command, "~/", 2) == 0) {
        str_gsub(&command_line.command, "~", HOME);
    }
    str_gsub(&command_line.command, "$$", p_pid);
    str_gsub(&command_line.command, "$!", p_last_background);
    str_gsub(&command_line.command, "$?", p_last_foreground);

    // EXPANSION and ASSIGNMENT of ARGUMENTS
    while (token != NULL && strcmp(token, "#") != 0){
        command_line.arguments[counter] = strdup(token);

        if (strncmp(command_line.arguments[counter], "~/", 2) == 0) {
          str_gsub(&command_line.arguments[counter], "~", HOME);
        }
        str_gsub(&command_line.arguments[counter], "$$", p_pid);
        str_gsub(&command_line.arguments[counter], "$!", p_last_background);
        str_gsub(&command_line.arguments[counter], "$?", p_last_foreground);


        token = strtok(NULL, IFS);
        counter++;  
    }

    // PARSE TOKENS "&" for background processes
    if (strcmp(command_line.arguments[counter - 1], "&") == 0) {
      command_line.background = true;
      command_line.arguments[counter - 1] = NULL;
      counter--;
    }

    // PARSING TOKENS "<" and ">" for I/O
    int n = counter;
    if (strcmp(command_line.arguments[n-2], "<") == 0 || strcmp(command_line.arguments[n-2], ">") == 0) {
      if (strcmp(command_line.arguments[n-2], "<") == 0) {
        if (strcmp(command_line.arguments[n-1], "<") != 0 && strcmp(command_line.arguments[n-1], ">") != 0 && strcmp(command_line.arguments[n-1], "&") != 0) {
          command_line.infile = strdup(command_line.arguments[n-1]);
          command_line.arguments[n-2] = NULL;
          input_file_flag = 1;

          if (strcmp(command_line.arguments[n-4], ">") == 0) {
            if (strcmp(command_line.arguments[n-3], "<") != 0 && strcmp(command_line.arguments[n-3], ">") != 0 && strcmp(command_line.arguments[n-3], "&") != 0) {
              command_line.outfile = strdup(command_line.arguments[n-3]);
              command_line.arguments[n-4] = NULL;
              output_file_flag = 1;
            }
          }
        }
      }
      else if (strcmp(command_line.arguments[n-2], ">") == 0) {
        if (strcmp(command_line.arguments[n-1], "<") != 0 && strcmp(command_line.arguments[n-1], ">") != 0 && strcmp(command_line.arguments[n-1], "&") != 0) {
          command_line.outfile = strdup(command_line.arguments[n-1]);
          command_line.arguments[n-2] = NULL;
          output_file_flag = 1;

          if (strcmp(command_line.arguments[n-4], "<") == 0) {
            if (strcmp(command_line.arguments[n-3], "<") != 0 && strcmp(command_line.arguments[n-3], ">") != 0 && strcmp(command_line.arguments[n-3], "&") != 0) {
              command_line.infile = strdup(command_line.arguments[n-3]);
              command_line.arguments[n-4] = NULL;
              input_file_flag = 1;
            }
          }
        }
      }
    }

    sigaction(SIGINT, &ignore_action, NULL);

    // EXIT
    if (strcmp(command_line.command, "exit") == 0) {
      if (counter > 2) {
        fprintf(stderr, "Too many arguments for exit command!\n");
        goto exit;
      }

      else if (counter == 2) {
        int exit_val = atoi(command_line.arguments[1]);
        if (!isdigit(*command_line.arguments[1])) {
          fprintf(stderr, "Argument provided not an integer value.\n");
          goto exit;
        }
        fprintf(stderr, "\nexit\n");
        kill(0, SIGINT);
        exit(exit_val);
      }

      else {
        fprintf(stderr, "\nexit\n");
        kill(0, SIGINT);
        exit(last_foreground);
      }
    }


    // CD
    if (strcmp(command_line.command, "cd") == 0) {
      if (counter > 2) {
        fprintf(stderr, "Too many arguments for cd command!\n");
        goto exit;
      }

      else if (counter == 2) {
        chdir(command_line.arguments[1]);
        goto exit;
      }

      else {
        chdir(HOME);
        goto exit;
      }
    }
 
    pid_t spawnPID = fork();
    int status;
    switch(spawnPID)
    {
      case -1:
          fprintf(stderr, "fork() failed\n");
          exit(1); 
          break;

      case 0:

          if(input_file_flag == 1){

            int sourceFD = open(command_line.infile, O_RDONLY); 
            if (sourceFD == -1) {
              fprintf(stderr, "source open() erro\n");
              exit(1);
            }
 
            int fd_in = dup2(sourceFD, 0); 
            if (fd_in == -1) {
              fprintf(stderr, "dup2() error on fd_in\n");
              exit(2);
           }

          close(sourceFD);
          
          }

          if (output_file_flag == 1) {
            
            int targetFD = open(command_line.outfile, O_RDWR | O_CREAT | O_TRUNC, 0777);
            if (targetFD == -1) {
              fprintf(stderr, "target open() error\n");
              exit(1);
            }

            int fd_out = dup2(targetFD, 1);
            if (fd_out == -1) {
              fprintf(stderr, "dup2() error on fd_out\n");
              exit(2);
            }

          close(targetFD);

          }

          execvp(command_line.command, command_line.arguments);
          fprintf(stderr, "Error with execvp I/O redirect\n");
          exit(1);
          

      default:

          if (command_line.background == 0) {
            
            if (waitpid(spawnPID, &status, WUNTRACED) == -1){
              fprintf(stderr, "wait issue, exiting\n");
              goto exit;
            }
            
            if (WIFEXITED(status)) {
              last_foreground = WEXITSTATUS(status);
            }

            else if (WIFSIGNALED(status)) {
              last_foreground = WTERMSIG(status) + 128;      
            }

            else if (WIFSTOPPED(status)) {
              kill(spawnPID, SIGCONT);
              fprintf(stderr, "Child process %jd stopped. Continuing.\n", (intmax_t) spawnPID);
              last_background = spawnPID; 
            }
          }

          else { last_background = spawnPID; }
    }

  exit:;
    
    memset(inputLine, 0, sizeof(*inputLine));

    if (counter > 0) {
      free(command_line.command);
    }

    if (input_file_flag == 1) {
      free(command_line.infile);
    }

    if (output_file_flag == 1) {
      free(command_line.outfile);
    }
    
    for (int i = 0; i != counter; i++) {
      free(command_line.arguments[i]);
    }

    free(p_last_foreground);
    command_line.background = 0;

  }  
  return 0;
}
