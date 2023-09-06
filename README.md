# smallsh

smallsh is a Linux shell written in C, implementing a command line interface similar to well-known shells, such as bash. 

This program:

- Prints an interactive input prompt
- Parses command line input into semantic tokens
- Implements parameter expansion:
  - Shell special parameters '$$', '$?', and '$!'
  - Tilde (~) expansion
- Implements two shell built-in commands: exit and cd
- Executes non-built-in commands
- Implements redirection operators ‘<’ and ‘>’
- Implements the ‘&’ operator to run commands in the background
- Implements custom behavior for SIGINT and SIGTSTP signals

A simple makefile has been included to build the program using the C99 standard.
