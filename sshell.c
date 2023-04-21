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
#define STRING_VARS_MAX 26


typedef struct process {
        char *cmdArray[ARG_MAX + 1];
        pid_t pid;
        int retval;
        bool outRedirect;
        char *outFile;
        bool outCombination;
        bool pipeCombinationAfter;
} Process;

typedef struct envVar{
        char letter;
        char *string;
} EnvVar;

void setString(char varLetter, char* str, EnvVar* strArr) {
        for (int i = 0; i < 26; i++) {
                if (i == varLetter-97) {
                        strArr[i].string = str;
                        // printf("str string: %s\n", strArr[i].string);
                }
        }
}

char* getString(char varLetter, EnvVar* strArr) {
        for (int i = 0; i < 26; i++) {
                // printf("I: %d, varLetter: %d\n", i, varLetter);
                if (i == varLetter-97) {
                        //printf("PRERETURN\n");
                        return strArr[i].string;
                }
        }
        return "";
}

void initializeVars(EnvVar* strArr) {
        char c = 'a';
        for (int i= 0; i < 26; i++) {
                strArr[i].letter = c;
                strArr[i].string = "";
                c++;
        }
}

bool checkIsValidVar(char varLetter) {
        char c = 'a';
        for (int i = 0; i < 26; i++) {
                // printf("Char c: %d, varLetter: %d \n", c, varLetter);
                if (c == varLetter)
                        return true;
                c++;
        }
        return false;
}

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

int fillTokens(char cmdline[CMDLINE_MAX], char tokens[ARG_MAX + 1][TOKEN_LENGTH_MAX + 1], EnvVar envVarArray[STRING_VARS_MAX], bool *runCommand) {     // returns how many tokens it had (max 16)
        int tokenIndex = 0;
        int stringIndex = 0;

        bool inString = false;
        bool inQuote = false;
        char quote = '\'';
        bool lastChar$ = false;

        char breaks[BREAK_ARR_SIZE] = {' ', '\t', '|', '>'};

        int cmdLen = strlen(cmdline);
        char c;
        for (int cmdIndex = 0; cmdIndex < cmdLen; ++cmdIndex) {
                c = cmdline[cmdIndex];
                if (inString) {
                        // check if string needs to be finalize
                        // 1. whitespace
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
                        if (isBreak) {
                                // need to close the current string
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
                                } else {
                                // still part of current string
                                tokens[tokenIndex][stringIndex++] = c;
                                }
                        }
                } else { // inString == false
                        if (tokenIndex == 16) {
                                fprintf(stderr, "Error: too many process arguments\n");
                                *runCommand = false;
                        }
                        // cases to check for : quote, pipe, redirect, not whitespace
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
                        } else if (c == '$') { 
                                lastChar$ = true;
                        } else if (lastChar$ == true) {
                                // printf("token index: %d\n", tokenIndex);
                                char *envString = getString(c, envVarArray);
                                // printf("string: %s\n", envString);
                                for (size_t i = 0; i < strlen(envString); i++) {
                                        // printf("envString i = %d, i = %ld \n", envString[i], i);
                                        tokens[tokenIndex][i]=envString[i];
                                }
                                tokens[tokenIndex][strlen(envString)] = '\0';
                                ++tokenIndex;
                                lastChar$ = false;
                                // printf("string2: %s\n", tokens[tokenIndex]);
                        } else if (c != ' ' && c != '\t') {
                                tokens[tokenIndex][stringIndex++] = c;
                                inString = true;
                        }
                }
        } // end for loop

        if (inString) {
                tokens[tokenIndex++][stringIndex] = '\0';
        }
        // printf("token INdex: %d\n", tokenIndex);
        return tokenIndex;
}

int breakIntoCommands(char tokens[ARG_MAX + 1][TOKEN_LENGTH_MAX + 1], int numTokens, Process proccessArray[COMMAND_NUM_MAX], bool *runCommand){
        int numCommands = 0;
        char* token = NULL;
        int curr = 0;

        bool lastTokenPipe = false;

        proccessArray[0].outRedirect = false;
        proccessArray[0].pid = 0;
        proccessArray[0].retval = 0;
        proccessArray[0].outCombination = false;
        proccessArray[0].pipeCombinationAfter = false;

        for(int i = 0; i < numTokens; i++) {
                token = tokens[i];
                if (!(strcmp(token, "|"))) {
                        if(i == 0 || i == numTokens-1) {
                                fprintf(stderr, "Error: missing command\n");
                                *runCommand = false;
                        }
                        // close this process
                        proccessArray[numCommands].cmdArray[curr] = NULL;
                        numCommands++;
                        curr = 0;
                        lastTokenPipe = true;
                        // initialize next process
                        proccessArray[numCommands].outRedirect = false;
                        proccessArray[numCommands].pid = 0;
                        proccessArray[numCommands].retval = 0;
                        proccessArray[numCommands].outCombination = false;
                        proccessArray[numCommands].pipeCombinationAfter = false;
                } else if(lastTokenPipe && !(strcmp(token, "&"))) {
                        proccessArray[numCommands-1].pipeCombinationAfter = true; 
                        lastTokenPipe = false;
                } else if (!(strcmp(token, ">"))){
                        if(i == 0) {
                                fprintf(stderr, "Error: missing command\n");
                                *runCommand = false;
                        } else if (i == numTokens-1) {
                                fprintf(stderr, "Error: no output file\n");
                                *runCommand = false;
                        } else if (i != numTokens-2) {
                                fprintf(stderr, "Error: mislocated output redirection\n");
                                *runCommand = false;
                        }
                        proccessArray[numCommands].outRedirect = true;
                        proccessArray[numCommands].cmdArray[curr] = NULL;
                        curr = 0;
                        lastTokenPipe = false;
                } else if (proccessArray[numCommands].outRedirect == true) {
                        if (!(strcmp(token, "&"))){
                                proccessArray[numCommands].outCombination = true;
                        } else {
                                proccessArray[numCommands].outFile = token;
                        }
                        lastTokenPipe = false;
                } else if (proccessArray[numCommands].outCombination == true){
                        proccessArray[numCommands].outFile = token;
                        lastTokenPipe = false;
                } else {
                        proccessArray[numCommands].cmdArray[curr++] = token;
                        lastTokenPipe = false;
                }
        }

        if (curr != 0){         // we didn't close last command
                proccessArray[numCommands].cmdArray[curr] = NULL;
        }

        return numCommands+1;
}

int main(void) {
        char cmdline[CMDLINE_MAX];
        char tokens[ARG_MAX + 1][TOKEN_LENGTH_MAX + 1]; // extra 1 bc last one will be NULL
        Process proccessArray[COMMAND_NUM_MAX];
        EnvVar envVarArray[STRING_VARS_MAX];
        initializeVars(envVarArray);
        bool runCommand = true;

        while (1) {
                char *nl;
                // int retval;

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
                int numTokens = fillTokens(cmdline, tokens, envVarArray, &runCommand);
                int numProcesses = breakIntoCommands(tokens, numTokens, proccessArray, &runCommand);

                /*
                for(int i=0 ; i<numTokens ; i++){
                        printf("index = %d , [%s]\n", i, tokens[i]);
                }
                printf("\n");
                */

                if (!strcmp(tokens[0], "pwd")) {     // builtin command pwd
                        char buffer[1000];
                        getcwd(buffer, 1000);
                        printf("%s\n", buffer);
                        fprintf(stderr, "+ completed '%s' [0]\n", cmdline);
                } else if(!strcmp(tokens[0], "cd")) {   // builtin command cd
                        int ch = chdir(tokens[1]);
                        // printf("%d \n", ch);    // 0 if successful, -1 if error
                        if (ch == -1) {
                                fprintf(stderr, "Error: cannot cd into directory\n");
                                ch = 1;
                        }
                        fprintf(stderr, "+ completed '%s' [%d]\n", cmdline, ch);
                } else if (!strcmp(tokens[0], "set")) { // builtin command set
                        // printf("SETTING");
                        // printf("Tokens[1]: %d \n", tokens[1][0]);
                        char envVarLet = tokens[1][0];
                        // printf("%d", envVarLet);
                        char *envVarStr = tokens[2];
                        if (checkIsValidVar(envVarLet)) {
                                setString(envVarLet, envVarStr, envVarArray);
                        } else {
                                printf("Error: invalid variable name\n");
                        }
                } else if (runCommand) {        // not a builtin command
/*
                        for(int i=0; i<numProcesses; i++){
                                printf("process number : %d\n", i);
                                printStruct(&proccessArray[i]);
                        }
*/
                        pid_t pid;
                        int fd[COMMAND_NUM_MAX-1][2];              
                        for(int i=0; i<numProcesses; i++){
                                if ( i < numProcesses-1 ){
                                        pipe( fd[i] );
                                }
                                pid = fork();
                                if (pid == 0){
                                        //child

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
                                                if (proccessArray[i].pipeCombinationAfter){
                                                        dup2(fd[i][1], STDERR_FILENO);
                                                }
                                                close(fd[i][1]);                   /* Close now unused FD */
                                        }
                                        
                                        if(proccessArray[i].outRedirect){
                                                int fd_out = open(proccessArray[i].outFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                                                dup2(fd_out, STDOUT_FILENO);
                                                if(proccessArray[i].outCombination){
                                                        dup2(fd_out, STDERR_FILENO);
                                                }
                                                close(fd_out);
                                        }
                                        
                                        execvp(proccessArray[i].cmdArray[0], proccessArray[i].cmdArray);
                                        fprintf(stderr, "Error: command not found\n");
                                        
                                        //perror("execvp");
                                        exit(1);
                                } else if (pid > 0) {
                                        // Parent 
                                        if ( i > 0 ){
                                                close(fd[i-1][0]);
                                                close(fd[i-1][1]);
                                        }
                                        proccessArray[i].pid = pid;
                                } else {
                                        perror("fork");
                                        exit(1);
                                }
                        }

                        for(int i=0; i<numProcesses; i++){
                                int status;
                                waitpid(proccessArray[i].pid, &status, 0);
                                proccessArray[i].retval = WEXITSTATUS(status);
                                //printf("Return status value for process: %d, return val: %d\n", i, proccessArray[i].retval);
                        }

                        fprintf(stderr, "+ completed '%s' ", cmdline);
                        for(int i=0; i<numProcesses; i++){
                               fprintf(stderr, "[%d]", proccessArray[i].retval);
                        }
                        fprintf(stderr, "\n");
                } 
        }

        return EXIT_SUCCESS;
}