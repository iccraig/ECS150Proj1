# SSHELL: Simple Shell

## Summary

This program, `sshell`, is a command-line interpreter- it accepts input from the
user under the form of command lines and executes them as jobs. It a few
builtin commands (exit, pwd, cd, set), and can handle output redirection,
piping, combined redirection, and simple environment variables.

## Implementation

The implementation of this program has three logic levels:

1. Breaking the command line into individual tokens ⋅⋅⋅*dealing with
input/output (I/O)
2. Parsing the tokens and breaking them into individual commands/processes
⋅⋅⋅*understanding the language of the shell (builtin commands, |, >, &, $...)
3. Running all the processes and managing files ⋅⋅⋅*forking, fd...

Each level of implementation depends on the one before it.

### Breaking cmdline into tokens

The fillTokens function parses through cmdline and separates it into individual
tokens (arguments). If we were in the middle of a token, we first needed to
check if we are in the middle of a quote, since then the whitespace needs to be
included in the token. The next check is if there is a character that breaks up
the tokens (whitespace, pipe, or output redirection). In this case, we needed to
close the current token. If we were not in the middle of a token, we needed to
first check if there were too many arguments. Secondly, we needed to check for
the beginning of a quote. Then, if there was a pipe, redirection, or combination
symbol, we made it into its own individual token. Lastly, if there was a regular
character, we added it to our current token. If there was a whitespace, the
function just continued parsing, which took care of the possibility of multiple
whitespaces between command line arguments. 

### Parsing tokens into processes

The breakIntoCommands function parses through the now individualized tokens, and
fills up "processArray" - an array of Process structs. Each Process represents
one command from the overall cmdline, which is broken up according to pipes.
Each Process structure has the command to be completed, the pid, return value,
whether or not the command's output needs to be redirected (and if it does what
the name of the output file should be), and whether there needs to be a
combination for the output redirection or piping. This provides us with all the
information later needed to run the processes. 

### Running processes

Running the processes occurs in the main section of our code within a while loop
that only breaks when the user calls the built-in command 'exit'. The shell
prints the shell prompt, gets the command line, then breaks it up first into
tokens (by calling fillTokens) and then into individual processes (by calling
breakIntoCommands). 

We then check for the builtin commands, which do not require forking. These
commands are exit, pwd, cd, and set. If the command was not a builtin command,
and there were no parsing errors (which would make runCommand false), the shell
then executes the requested command. 

We set up an array of file descriptor arrays, each of size 2 (so we can have
fd[0] for reading and fd[1] for writing). Our for loop runs according to the
number of processes requested. We make a pipe of the current fd array only if
needed, then fork(). The child first connects to the correct pipes. If it is the
first command, it only connects to the pipe after it. If it is in the middle, if
connects both to pipes before it and after it. If it is the last command, it
only connects to the pipe before it. If the output of the process needs to be
redirected to a different file, the child sets up that connection. Finally, the
child executes the command, saved in the struct as cmdArray. 

The parent closes the correct file descriptors, and saves the pid in the struct.
We wait for all the processes to finish, saving all of their return values in
the structs. 

Finally, we can display a completion message, consisting of the cmdline executed
and all of the return values of the commands we executed. 

The program returns to the beginning of the while loop, prompting the user for a
new command line.


### 