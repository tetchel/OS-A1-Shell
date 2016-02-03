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
#define HIST_MAX    10

//who knew that C had volatile!
volatile int term_requested = 0;

//return name of user who invoked the shell
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

void check_valid_alloc(void* to_check, char* caller) {
    if(!to_check) {
	fprintf(stderr, "Error allocating memory in %s! Aborting.\n", caller);
        exit(EXIT_FAILURE);
    }
}
//splits a string into an array of substrings, delimited by spaces
//num_tokens_out out parameter with token count
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
    *num_tokens_out = space_count;
    return result;
}

//execute without redirection or pipes
int exec_normal(char** tokens) {
    pid_t pid = fork();
    if(pid > 0) {
        //parent
        wait(NULL);
    }
    else if (pid < 0) {
        perror("Forking error");
	exit(EXIT_FAILURE);
    }
    else {
        //child
        execvp(tokens[0], tokens);
        //this is an error because exec should have replaced the below code
	perror("Exec error");
    }
    return 0;
}

//SIGTERM handler
//I suppose this should also kill any children
void terminate(int signum) {
    term_requested = 1;
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
    //allocate for user input
    char* input = malloc(COMMAND_LEN);
    check_valid_alloc(input, "main_input");
    //history is stored as one long string delimited by \n
    char* history;
    //once it reaches HIST_SIZE, start replacing old entries
    int hist_size = 0;
    //main program loop
    //while input is not exit, loop and get more input.
    //while(strcmp(input, "exit") && !term_requested) {
    while(1) {
        //prompt
        printf("%s> ", username);
        //read until newline, discard anything after it
        scanf("%[^\n]%*c", input);

        //check if deletion of oldest history is required
        if(hist_size >= HIST_MAX) {
            //delete up to the first \n
            char* new_history = strstr(history, "\n");
            //more asprintf-ing while trying to avoid leaks
            char* tmp;
            //add 1 to delete the first \n
            asprintf(&tmp, "%s", new_history+1);
            free(history);
            free(new_history);
            asprintf(&history, "%s", tmp);
            free(tmp);
        }
        //once hist_size reaches HIST_MAX, it just stays there
        else if (hist_size <= HIST_MAX) {
            hist_size++;
        }
        //add to history
        if(hist_size > 1) {
            //append the newest history item
            //more complex than it needs to be to prevent asprintf from leaking
            //every time it's called
            char* tmp;
            asprintf(&tmp, "%s\n%s", history, input);
            free(history);
            asprintf(&history, "%s", tmp);
            free(tmp);
        }
        else {
            //first item, nothing fancy needed
            asprintf(&history, "%s", input);
        }

        //if they entered "history", output it now that it is up-to-date
        if(!strcmp(input, "history")) {
            printf("%s\n", history);
        }
	else if(!strcmp(input, "exit") || term_requested) {
	    printf("Exit requested\n");
	    return 0;
	}
        //else, it's a command, analyze to figure out what to do next
        else {
            //tokenize input to determine what kind of command we have
            int num_tokens, i, command_type = 0;
            char** tokens = tokenize_string(input, &num_tokens);
            for(i = 0; i < num_tokens; i++) {
                //loop through tokens looking for redirection or pipe
                printf("Token: %s\n", tokens[i]);
                if(!strcmp(tokens[i], "<") || !strcmp(tokens[i], ">")) {
                    //redirection
		    command_type = 1;
                    printf("Redir\n");
                }
                else if(!strcmp(tokens[i], "|")) {
                    //one or more pipes
		    command_type = 2;
                    printf("Pipe\n");
                }
            }
            //no redirection of any kind
	    if(command_type == 0) {
		exec_normal(tokens);
	    }            
        }
    }   //end MAIN LOOP
    //exit was entered or sigterm
    //printf("Exit requested\n");
    return 0;
}

