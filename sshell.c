#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdbool.h>
#include <sys/wait.h>

#define CMDLINE_MAX 512
#define ARG_MAX 16
#define TOKEN_LENGTH_MAX 32
#define BREAK_ARR_SIZE 4
#define COMMAND_NUM_MAX 4       // We assume that there can be up to three pipe signs on the same command line to connect up to four commands to each other
#define ENVIR_VAR_MAX 26
#define PWD_MAX 1000

typedef struct process {
        char *cmdArray[ARG_MAX + 1];
        pid_t pid;
        int retval;
        bool outRedirect;
        char *outFile;
        int outFileFD;
        bool outCombination;
        bool pipeCombinationAfter;
} Process;

void initializeProcess(Process* proc){
        proc->pid = 0;
        proc->retval = 0;
        proc->outRedirect = false;
        proc->outFileFD = 0;
        proc->outCombination = false;
        proc->pipeCombinationAfter = false;
}

/*
void printStruct(Process *proc){
        int i=0;
        while(proc->cmdArray[i] != NULL){
                printf("cmdArray : <%s>\n", proc->cmdArray[i]);
                i++;
        }
        printf("outRedirect : <%d> ; outCombination: <%d>\n", proc->outRedirect, proc->outCombination);
        if(proc->outRedirect){
                printf("outFile : <%s>\n", proc->outFile);
        }
        printf("pipeCombinationAfter : %d\n", proc->pipeCombinationAfter);
        printf("\n");
        
}

void printEnviVar(char envirnVariables[ENVIR_VAR_MAX][TOKEN_LENGTH_MAX + 1], char* title){
        printf("%s:", title);
        for(int i=0; i<ENVIR_VAR_MAX; i++){
                printf("%d<%s>,",i, envirnVariables[i]);
        }
        printf("\n");
}
*/

int fillTokens(char cmdline[CMDLINE_MAX], char tokens[ARG_MAX + 1][TOKEN_LENGTH_MAX + 1], bool *runCommand) {     // returns how many tokens it had (max 16)
        int tokenIndex = 0;
        int stringIndex = 0;
        bool inString = false;
        bool inQuote = false;
        char quote = '\'';
        char breaks[BREAK_ARR_SIZE] = {' ', '\t', '|', '>'};

        int cmdLen = strlen(cmdline);
        char c;
        for (int cmdIndex = 0; cmdIndex < cmdLen; ++cmdIndex) {
                c = cmdline[cmdIndex];
                if (inString) {
                        // check if string needs to be finalize 
                        // 1. whitespace & quotes
                        // 2. redirect
                        // 3. pipe
                        if (inQuote) {
                                if (c == quote) {
                                        inQuote = false;
                                } else { // char is part of the string anyways
                                        tokens[tokenIndex][stringIndex++] = c;
                                }
                        } else if (c == '\"' || c == '\'') {
                                quote = c;
                                inQuote = true;
                        } else {
                                bool isBreak = false;
                                for (int i = 0; !isBreak && i < BREAK_ARR_SIZE; ++i) {
                                        isBreak = (breaks[i] == c);
                                }
                                if (isBreak) {  // need to close the current string
                                        tokens[tokenIndex][stringIndex] = '\0';
                                        ++tokenIndex;
                                        stringIndex = 0;
                                        inString = false;
                                        if (c == '|') {
                                                tokens[tokenIndex][0] = '|';
                                                tokens[tokenIndex][1] = '\0';
                                                ++tokenIndex;
                                        } else if (c == '>') {
                                                tokens[tokenIndex][0] = '>';
                                                tokens[tokenIndex][1] = '\0';
                                                ++tokenIndex;
                                        }
                                } else {  // still part of current string
                                        tokens[tokenIndex][stringIndex++] = c;
                                }
                        }
                } else { // inString == false
                        if (tokenIndex == 16) {
                                fprintf(stderr, "Error: too many process arguments\n");
                                *runCommand = false;
                        }
                        // cases to check for : quote, pipe, redirect, combination, environment variable, not whitespace
                        if (c == '\"' || c == '\'') {
                                quote = c;
                                inQuote = true;
                                inString = true;
                        } else if (c == '|') {
                                tokens[tokenIndex][0] = '|';
                                tokens[tokenIndex][1] = '\0';
                                ++tokenIndex;
                        } else if (c == '>') {
                                tokens[tokenIndex][0] = '>';
                                tokens[tokenIndex][1] = '\0';
                                ++tokenIndex;
                        } else if (c == '&') {
                                tokens[tokenIndex][0] = '&';
                                tokens[tokenIndex][1] = '\0';
                                ++tokenIndex;
                        } else if (c != ' ' && c != '\t') {
                                tokens[tokenIndex][stringIndex++] = c;
                                inString = true;
                        }
                }
        } // end for loop

        if (inString) {
                tokens[tokenIndex++][stringIndex] = '\0';
        }
        return tokenIndex;
}

int breakIntoCommands(char tokens[ARG_MAX + 1][TOKEN_LENGTH_MAX + 1], int numTokens, Process processArray[COMMAND_NUM_MAX], char envirnVariables[ENVIR_VAR_MAX][TOKEN_LENGTH_MAX + 1], bool *runCommand){
        int numCommands = 0;
        char* token = NULL;
        int curr = 0;
        bool lastTokenPipe = false;
        initializeProcess(&processArray[0]);

        for(int i = 0; i < numTokens; i++) {
                token = tokens[i];
                if (!(strcmp(token, "|"))) {
                        if(i == 0 || i == numTokens-1) {
                                fprintf(stderr, "Error: missing command\n");
                                *runCommand = false;
                        }
                        // close this process
                        processArray[numCommands].cmdArray[curr] = NULL;
                        numCommands++;
                        curr = 0;
                        lastTokenPipe = true;
                        // initialize next process
                        initializeProcess(&processArray[numCommands]);
                } else if(lastTokenPipe && !(strcmp(token, "&"))) {
                        processArray[numCommands-1].pipeCombinationAfter = true; 
                        lastTokenPipe = false;
                } else if (!(strcmp(token, ">"))){
                        if(i == 0) {
                                fprintf(stderr, "Error: missing command\n");
                                *runCommand = false;
                        } else if (i == numTokens-1) {
                                fprintf(stderr, "Error: no output file\n");
                                *runCommand = false;     
                        } 
                        processArray[numCommands].outRedirect = true;
                        processArray[numCommands].outFileFD = 0;
                        processArray[numCommands].cmdArray[curr] = NULL;
                        curr = 0;
                        lastTokenPipe = false;
                } else if (processArray[numCommands].outRedirect == true) {
                        if (!(strcmp(token, "&"))){
                                processArray[numCommands].outCombination = true;
                        } else {
                                processArray[numCommands].outFile = token;
                        }
                        lastTokenPipe = false;
                } else if (processArray[numCommands].outCombination == true){
                        processArray[numCommands].outFile = token;
                        lastTokenPipe = false;
                } else {
                        if(strlen(token) == 2 && token[0] == '$' && token[1] >= 'a' && token[1] <= 'z'){
                                processArray[numCommands].cmdArray[curr++] = envirnVariables[token[1]-97];
                        } else {
                                processArray[numCommands].cmdArray[curr++] = token;
                        }
                        lastTokenPipe = false;
                }
        }

        if (curr != 0){         // we didn't close last command
                processArray[numCommands].cmdArray[curr] = NULL;
        }

        return numCommands+1;
}

int main(void) {
        char cmdline[CMDLINE_MAX];
        char tokens[ARG_MAX + 1][TOKEN_LENGTH_MAX + 1]; // extra 1 bc last one will be NULL
        Process processArray[COMMAND_NUM_MAX];
        char envirnVariables[ENVIR_VAR_MAX][TOKEN_LENGTH_MAX + 1];
        bool runCommand = true;

        for(int i=0; i<ENVIR_VAR_MAX; i++){
                envirnVariables[i][0] = '\0';
        }

        while (1) {
                char *nl;

                /* Print prompt */
                printf("sshell@ucd$ ");
                fflush(stdout);

                /* Get command line */
                fgets(cmdline, CMDLINE_MAX, stdin);

                /* Print command line if stdin is not provided by terminal */
                if (!isatty(STDIN_FILENO)) {
                printf("%s", cmdline);
                fflush(stdout);
                }

                /* Remove trailing newline from command line */
                nl = strchr(cmdline, '\n');
                if (nl)
                        *nl = '\0';

                /* Builtin command exit */
                if (!strcmp(cmdline, "exit")) {
                        fprintf(stderr, "Bye...\n");
                        fprintf(stderr, "+ completed 'exit' [0]\n");
                        break;
                }

                runCommand = true;
                int numTokens = fillTokens(cmdline, tokens, &runCommand);
                int numProcesses = breakIntoCommands(tokens, numTokens, processArray, envirnVariables, &runCommand);           

                if (!strcmp(tokens[0], "pwd")) {     // builtin command pwd
                        char buffer[PWD_MAX];
                        getcwd(buffer, PWD_MAX);
                        printf("%s\n", buffer);
                        fprintf(stderr, "+ completed '%s' [0]\n", cmdline);
                } else if (!strcmp(tokens[0], "cd")) {   // builtin command cd
                        int ch = chdir(tokens[1]);
                        if (ch == -1) {
                                fprintf(stderr, "Error: cannot cd into directory\n");
                                ch = 1;
                        }
                        fprintf(stderr, "+ completed '%s' [%d]\n", cmdline, ch);
                } else if (!strcmp(tokens[0], "set")) { // builtin command set
                        if( tokens[1] != NULL && strlen(tokens[1]) == 1 && tokens[1][0] >= 'a' && tokens[1][0] <= 'z'){
                                if(tokens[2] != NULL) {
                                        strcpy(envirnVariables[tokens[1][0] - 97], tokens[2]);
                                } else {
                                        envirnVariables[tokens[1][0] - 97][0] = '\0';
                                }   
                        } else {
                                fprintf(stderr, "Error: invalid variable name\n");
                        }
                        fprintf(stderr, "+ completed '%s' [0]\n", cmdline);
                } else if (runCommand) {        // not a builtin command
                        pid_t pid;
                        int fd[COMMAND_NUM_MAX-1][2]; 

                        bool redirectSuccedeed = true;
                        for (int i = 0; i < numProcesses ; ++i ){
                                if(processArray[i].outRedirect){
                                        processArray[i].outFileFD = open(processArray[i].outFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                                        if (processArray[i].outFileFD < 0 ){
                                                redirectSuccedeed = false;
                                        }                              
                                }
                        }

                        for(int i=0; i<numProcesses; i++){
                                if ( i < numProcesses-1 ){
                                        pipe( fd[i] );
                                }
                                pid = fork();
                                if (pid == 0){  //child
                                        // check if not first --> need to open pipe from before
                                        if ( i > 0) {
                                                close(fd[i-1][1]);                 /* No need for write access */
                                                dup2(fd[i-1][0], STDIN_FILENO);    /* Replace stdin with pipe */
                                                close(fd[i-1][0]);                 /* Close now unused FD */
                                        }

                                        // check if not last --> need to open pipe for after 
                                        if(i < numProcesses-1) {
                                                close(fd[i][0]);                   /* No need for read access */
                                                dup2(fd[i][1], STDOUT_FILENO);     /* Replace stdout with pipe */
                                                if (processArray[i].pipeCombinationAfter){
                                                        dup2(fd[i][1], STDERR_FILENO);
                                                }
                                                close(fd[i][1]);                   /* Close now unused FD */
                                        }
                                        
                                        if(processArray[i].outRedirect){
                                                if(processArray[i].outFileFD >= 0){
                                                        dup2(processArray[i].outFileFD, STDOUT_FILENO);
                                                        if(processArray[i].outCombination){
                                                                dup2(processArray[i].outFileFD, STDERR_FILENO);
                                                        }
                                                        close(processArray[i].outFileFD);
                                                }
                                        }

                                        if (redirectSuccedeed){
                                                execvp(processArray[i].cmdArray[0], processArray[i].cmdArray);
                                                fprintf(stderr, "Error: command not found\n");
                                        }
                                        exit(1);
                                } else if (pid > 0) {   // Parent
                                        if ( i > 0 ){
                                                close(fd[i-1][0]);
                                                close(fd[i-1][1]);
                                        }
                                        processArray[i].pid = pid;
                                } else {
                                        perror("fork");
                                        exit(1);
                                }
                        }

                        for(int i=0; i<numProcesses; i++){
                                int status;
                                waitpid(processArray[i].pid, &status, 0);
                                processArray[i].retval = WEXITSTATUS(status);
                        }

                        if(redirectSuccedeed) {
                                fprintf(stderr, "+ completed '%s' ", cmdline);
                                for(int i=0; i<numProcesses; i++){
                                        fprintf(stderr, "[%d]", processArray[i].retval);
                                }
                                fprintf(stderr, "\n");
                        } else {
                                fprintf(stderr, "Error: cannot open output file\n");
                        }   
                } 
        }

        return EXIT_SUCCESS;
}