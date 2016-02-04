#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <pwd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>

#define COMMAND_LEN 1024    //MAXIMUM command length
#define HIST_MAX    10	    //number of items to hold in history

//whether or not user has pressed ctrl+c (starts false)
volatile int term_requested = 0;

//return name of user who invoked the shell
//adapted from http://stackoverflow.com/questions/8953424/how-to-get-the-username-in-c-c-in-linux
const char* get_username() {
    uid_t uid = geteuid();		    //returns effective userid of current user
    struct passwd* pw = getpwuid(uid);	    //get passwd for that user
    const char* retval = pw->pw_name;	    //get username from the passwd

    //return if valid result, else perror and return ""
    if (retval)
        return retval;
    //should not happen, but just in case
    else {
        perror("Error getting username");
        return "jdoe";
    }
}

//quick function to call after alloc to make sure it didn't return NULL
void check_valid_alloc(void* to_check, char* caller) {
    if(!to_check) {
	fprintf(stderr, "Error allocating memory in %s! Aborting.\n", caller);
        exit(EXIT_FAILURE);
    }
}
//splits a string into an array of substrings, delimited by spaces
//num_tokens_out out parameter with token count
//adapted from http://stackoverflow.com/questions/11198604/c-split-string-into-an-array-of-strings
char** tokenize_string(char* s, int* num_tokens_out) {
    char** result = NULL;
    //current index in the string being tokenized
    char* index = strtok(s, " ");
    //no. spaces in the input = size of the array - 1
    int space_count = 0;

    //strtok will return NULL once there are no more tokens
    //so loop until that happens
    while(index) {
        //expand the array for new element
        result = realloc(result, sizeof(char*) * ++space_count);
        check_valid_alloc(result, "tokenize_string");
        //add the item at index to the array of tokens
        result[space_count-1] = index;
        //advance to the next token
        index = strtok(NULL, " ");
    }
    //add a NULL to the end so it plays nicely with execv
    result = realloc(result, sizeof(char*) * space_count + 1);
    check_valid_alloc(result, "tokenize_string_2");
    result[space_count] = NULL;

    //update out parameter
    *num_tokens_out = space_count;
    return result;
}

//execute a command without redirection or pipes
void exec_normal(char** tokens) {
    pid_t pid = fork();
    if(pid > 0) {
        //parent (shell)
        wait(NULL);
    }
    else if (pid < 0) {
        perror("Forking error! Aborting");
        return;
    }
    else {
        //child
        execvp(tokens[0], tokens);
        //this is an error because exec should have replaced the below code
        perror("Exec error");
    }
}

//execute a command with redirection (one or two way)
void exec_redir(char** tokens) {

}

//execute a command with any number of pipes
void exec_pipe(char** tokens, int no_commands, int command_indices[]) {
	pid_t pid;
	if((pid = fork()) < 0) {
	    perror("Fork error! Aborting.");
	    return;
	}
	else if(pid > 0) {
	    //parent (shell process)
	    wait(NULL);
	}
	else {
	    //"oldest" child (rightmost command)
	    int i;
	    for(i = 0; i < no_commands;) {
            int pipe_fd[2];
                if(pipe(pipe_fd) < 0) {
                perror("Pipe error! Aborting.");
                return;
            }
            if((pid = fork()) < 0) {
                perror("Fork error! Aborting.");
                return;
            }
            else if(pid > 0) {
                //close read end
                close(pipe_fd[0]);
                if(dup2(pipe_fd[1], STDOUT_FILENO) < 0) {
                    perror("Dup error! Aborting");
                    return;
                }
                execvp(tokens[command_indices[i]], &tokens[command_indices[i]]);
            }
            else {
                //increment i so we execv the correct command
                i++;
                //child to run reading command
                close(pipe_fd[1]);
                if(dup2(pipe_fd[0], STDIN_FILENO) < 0) {
                    perror("Dup error! Aborting");
                    return;
                }
                execvp(tokens[command_indices[i]], &tokens[command_indices[i]]);
            }
	    }
	}
}

//SIGTERM handler
//I suppose this should also kill any children
void terminate(int signum) {
    term_requested = 1;
}

char* update_history(char* history, char* input, int* hist_size) {
    //check if deletion of oldest history is required
    if(*hist_size >= HIST_MAX) {
        //delete up to the first \n
        char* new_history = strstr(history, "\n");
        //add 1 to delete the first \n
        asprintf(&history, "%s", new_history+1);
    }
    //once hist_size reaches HIST_MAX, it just stays there
    else if (*hist_size <= HIST_MAX) {
        (*hist_size)++;
    }
    //add to history
    if(*hist_size > 1) {
        //append the newest history item
        asprintf(&history, "%s\n%s", history, input);
    }
    else {
        //first item, nothing fancy needed
       asprintf(&history, "%s", input);
    }
    return history;
}

//main function, contains IO loop, handles history functionality
//all real work is done by other functions
int main() {
    //SIGTERM handling code
    struct sigaction action;
    memset(&action, 0, sizeof(struct sigaction));
    action.sa_handler = terminate;
    sigaction(SIGTERM, &action, NULL);
    //end term handler

    //Start by obtaining username for prompt
    const char* username = get_username();
    //history is stored as one long string delimited by \n
    char* history;
    //once it reaches HIST_SIZE, start replacing old entries
    int hist_size = 0;

    //indices at which commands start
    int command_indices[256] = {0};
    //current index in the above array (as elements are added)
    //the 0th command starts is at tokens[0], this is already known
    int command_counter = 1;

    //main program loop
    //exit condition is lower down, if "exit" is entered or term_requested == true
    int cont = 1;
    while(cont) {
        //allocate for user input
        char* input = malloc(COMMAND_LEN);
        check_valid_alloc(input, "main_input");
        //prompt
        printf("%s> ", username);
        //get input
        fgets(input, COMMAND_LEN, stdin);

        //TODO this still does not work as intended!
        //if input is not empty
        history = update_history(history, input, &hist_size);

        //if they entered "history", output it now that it is up-to-date
        if(!strcmp(input, "history")) {
            printf("%s\n", history);
        }
        //if they entered "exit" or sent SIGTERM, exit
        else if(!strcmp(input, "exit") || term_requested) {
            printf("Exit requested\n");
            cont = 0;
        }
        //else, it's a command, analyze to figure out what to do next
        else {
            //tokenize input to determine what kind of command we have
            int num_tokens, i, command_type = 0;
            char** tokens = tokenize_string(input, &num_tokens);

            for(i = 0; i < num_tokens; i++) {
                //check for invalid command flag
                if(command_type == -1)
                    break;
                //loop through tokens looking for redirection or pipe
                if(!strcmp(tokens[i], "<")) {
                    command_indices[command_counter++] = i;
                    if(command_type == 1 || command_type > 4)
                        //this indicates that there are two '<'s or a pipe, which is not valid
                        command_type = -1;
                    else if(command_type == 2)
                        //this indicates that the command has > followed by <
                        command_type = 3;
                    else
                        command_type = 1;
                }
                else if(!strcmp(tokens[i], ">")) {
                    command_indices[command_counter++] = i;
                    if(command_type == 2 || command_type > 4)
                        //two '>'s or pipe, not valid
                        command_type = -1;
                    else if(command_type == 1)
                        // < followed by >
                        command_type = 4;
                    else
                        command_type = 2;
                }
                else if(!strcmp(tokens[i], "|")) {
                    command_indices[command_counter++] = i;
                    //one or more pipes
                    if(command_type >= 2 && command_type <= 4)
                        command_type = -1;
                    else
                        command_type = 5;
                }
            }
            //no redirection of any kind
            if(command_type == -1)
                printf("%s is not a valid command, please try again.\n", input);
            else if(command_type == 0)
                exec_normal(tokens);
            else if(command_type >= 2 && command_type <= 4)
                exec_redir(tokens);
            else if(command_type == 5)
                //#pipes 	= command_type - 4
                //#commands = # pipes + 1
                exec_pipe(tokens, command_counter, command_indices);
        }   //end command execution block
    }   //end MAIN LOOP
    return 0;
}

