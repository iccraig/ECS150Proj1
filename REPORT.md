# SSHELL: Simple Shell

## Summary

This program, `sshell`, is a command-line interpreter- it accepts input from the
user in the form of command lines and executes them as jobs. The shell supports
a number of builtin commands (exit, pwd, cd, set), and can handle output
redirection, piping, combined redirection and piping, and simple environment
variables.

## Implementation

The implementation of this program has three logic layers:

1. Parsing the command line into individual tokens ⋅⋅⋅*dealing with input/output
   (I/O)
2. Understanding the tokens, identifying builtin commands, and breaking them
   into individual commands/processes ⋅⋅⋅*understanding the language of the
   shell (builtin commands, |, >, &, $)
3. Executing the processes and managing files ⋅⋅⋅*forking, piping, fd

Each layer of implementation depends on the one before it.

### Parsing cmdline into tokens

The fillTokens() function parses through the command line arguments and
separates them into individual tokens. While in the middle of a token, we first
needed to check if we are in the middle of a quote, since then the whitespace
needs to be included in the token. The next check is if there is a character
that breaks up the tokens (whitespace, pipe, or output redirection). In this
case, we needed to close the current token. While not in the middle of a token,
we needed to check for the beginning of a quote. Then, if there was a pipe,
redirection, or combination symbol, we made it into its own individual token.
Lastly, if there was a regular character, we added it to our current token. If
there was a whitespace, the function just continued parsing, which took care of
the possibility of multiple whitespaces between command line arguments. 

### Parsing tokens into processes

The breakIntoCommands() function parses through the now individualized tokens,
and fills up "processArray" - an array of Process structs. Each Process
represents one command from the overall command line, which is broken up
according to pipes. Each Process structure has the command to be completed, the
pid, return value, whether or not the command's output needs to be redirected
(and if it does what the name of the output file should be), and whether there
needs to be a combination for the output redirection or piping. This provides us
with all the information later needed to run the processes. Additionally, the
environment variables are pre-processed and replaced with their corresponding
values, saved in a 2D array where the environment variable letter is key, and
the corresponding strings are values. Since environment variables are shared
between different command lines, we decided to create a copy of the tokens and
store them into the array instead of using pointers to the changing tokens. 

### Running processes

Running the processes occurs in the main section of our code within a while loop
that only breaks when the user calls the built-in command 'exit'. The shell
prints the shell prompt and gets the command line. It breaks it up first into
tokens by calling fillTokens(), and then into individual processes by calling
breakIntoCommands(). 

We then check for the builtin commands, which do not require forking. These
commands are exit, pwd, cd, and set. If the command was not a builtin command,
and there were no parsing errors, the shell then executes the requested command.
In addition, the main loop checks for all required file descriptors' validity
for later use regarding output redirection. 

For multiple piping, we set up an array of file descriptor arrays, each of size
2, so that we can have fd[0] for reading and fd[1] for writing. Our for loop
runs according to the number of processes requested. We make a pipe of the
current fd array only if needed, then fork(). For every two consecutive
processes, there is one distinct pipe. The first of the two connects its STDOUT
to the pipe, and the second connects its STDIN to the pipe. If there are more
than one pipe, processes may do both.  
If the output of the process needs to be redirected to a different file, the
child sets up that connection. Finally, the child executes the command that was
saved in the Process struct. 

The parent waits on all the processes to finish, saving all of their return
values in the corresponding Process structs. 

Finally, we can display a completion message, consisting of the command line
executed and all of the return values of the commands we executed. 

The program returns to the beginning of the while loop, prompting the user for a
new command line.

### Testing the Project 

Throughout the coding process, we continuously tested our project by:
1. Extensive printing to reflect each function's output to check the
   application's correctness.
2. Comparing our output for shell commands with the output on the terminal.
3. Running the given tester.   

### Sources Used 

To complete the project, we used the class slides and manual pages for c
commands. 