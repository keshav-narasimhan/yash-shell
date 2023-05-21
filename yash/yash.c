#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <unistd.h>
#include <signal.h>
#include <readline/readline.h>      /* remember to use -lreadline command when compiling yash.c */
#include <readline/history.h>

typedef struct redirect_t {
    char *leftInput;        /* input file for left child */
    char *leftOutput;       /* output file for left child */
    char *leftError;        /* error file for left child */
    int leftCmdStop;        /* index when redirections begin for left child */

    char *rightInput;       /* input file for right child (when pipe present) */
    char *rightOutput;      /* output file for right child (when pipe present) */
    char *rightError;       /* error file for right child (when pipe present) */
    int rightCmdStop;       /* index when redirections begin for right child (when pipe present) */
} Redirections;

typedef enum status_t {
    RUNNING,                /* running job */
    STOPPED,                /* stopped job */
    DONE                    /* finished job */
} Status;

typedef struct job_t {
    char *userInput;        /* user input */
    char **cmd_tokenized;   /* used for no pipes */
    char **leftPipe;        /* left pipe cmd */
    char **rightPipe;       /* right pipe cmd */
    char **leftStrip;       /* left pipe stripped for execvp */
    char **rightStrip;      /* right pipe stripped for execvp */
    int numTokens;          /* number of tokens in command */
    int pipeIndex;          /* index of the pipe (if there is one) */
    int cpid_1;             /* will also be pgid */
    int cpid_2;             /* cpid of right child --> only if there is a pipe */
    int job_id;             /* job id to be displayed for special commands */
    bool isBg;              /* is the job in the background */
    bool execError;
    struct job_t *nextJob;  /* linked list purposes */
    Status job_status;      /* status of job */
} Job;

/* LinkedList of jobs */
Job *head;
int pid_yash;

void freeJob(Job *job) {
    free(job->userInput);
    job->userInput = (char*)NULL;

    /*for (int i = 0; i < job->numTokens; i++) {
        free(job->cmd_tokenized[i]);
    }*/
    free(job->cmd_tokenized);
    job->cmd_tokenized = (char**)NULL;

    if (job->leftPipe != (char**)NULL) {
        free(job->leftPipe);
        job->leftPipe = (char**)NULL;
    }
    if (job->leftStrip != (char**)NULL) {
        free(job->leftStrip);
        job->leftStrip = (char**)NULL;
    }

    if (job->pipeIndex != -1) {
        free(job->rightPipe);
        free(job->rightStrip);
        job->rightPipe = (char**)NULL;
        job->rightStrip = (char**)NULL;
    }

    Job *makeNull = job;
    free(job);
    makeNull = (Job*)NULL;
}

/*void deleteErrors() {
    Job *before = head;
    Job *curr = head->nextJob;
    Job *temp;

    while (curr != (Job*)NULL) {
        printf(">>> %s, %d <<<\n", curr->userInput, curr->execError);
        if (curr->execError == true) {
            temp = curr;
            curr = curr->nextJob;
            before->nextJob = curr;
            temp->nextJob = (Job*)NULL;
            freeJob(temp);

            if (curr == (Job*)NULL) {
                return;
            }
            //curr = curr->nextJob;
            //before = before->nextJob;
        } else {
            before = before->nextJob;
            curr = curr->nextJob;
        }
    }
} */

Job *findPrevJob(int pgid) {
    Job *currJob = head;

    while (currJob->nextJob != (Job*)NULL) {
        if (currJob->nextJob->cpid_1 == pgid) {
            return currJob;
        } /*else if (currJob->nextJob->cpid_2 == pgid) {
            return currJob;
        }*/

        currJob = currJob->nextJob;
    }

    return (Job*)NULL;
}

/*void showDoneJobs() {
    int largestDone = -1;
    Job *curr = head->nextJob;

    while (curr != (Job*)NULL) {
        if (curr->job_status == DONE) {
            if (curr->job_id > largestDone) {
                largestDone = curr->job_id;
            }
        }

        curr = curr->nextJob;
    }

    curr = head->nextJob;
    while (curr != (Job*)NULL) {
        if (curr->job_status == DONE) {
            if (curr->job_id == largestDone)
        }
    }
}*/

void sigchld_handler(int num) {
    int cpid;
    Job *prev_job;
    Job *curr_job;

    while ((cpid = waitpid(-1, NULL, WNOHANG)) > 0) {
        prev_job = findPrevJob(cpid);
        if (prev_job == (Job*)NULL) {
            continue;
        }
        curr_job = prev_job->nextJob;
        if (curr_job->isBg == true) {
            // printf("[%d]+\tDONE\t\t%s\n", prev_job->nextJob->job_id, prev_job->nextJob->userInput);
            curr_job->job_status = DONE;
            /*Job *next = curr_job->nextJob;
            prev_job->nextJob = next;
            curr_job->nextJob = head->nextJob;
            head->nextJob = curr_job;*/
        } else {
            curr_job = prev_job->nextJob;
            prev_job->nextJob = curr_job->nextJob;
            curr_job->nextJob = (Job*)NULL;
            freeJob(curr_job);
        }
    }
}

void sigtstp_handler(int num) {
    // printf("\n>>> entered SIGTSTP <<<\n");
    int wstatus;

    Job *currJob = head->nextJob;
    Job *currFgJob = (Job*)NULL;

    /* find the current foreground job */
    while (currJob != (Job*)NULL) {
        if (currJob->isBg == false && currJob->job_status == RUNNING) {
            currFgJob = currJob;
        }

        currJob = currJob->nextJob;
    }

    /* nothing to execute */
    if (currFgJob == (Job*)NULL) {
        return;
    }

    /* change the job status and send the SIGTSTP signal */
    //kill(-(currFgJob->cpid_1), SIGTSTP);
    kill(currFgJob->cpid_1, SIGTSTP);
    if (currFgJob->pipeIndex != -1) {
        kill(currFgJob->cpid_2, SIGTSTP);
    }
    //waitpid(-(currFgJob->cpid_1), &wstatus, WNOHANG);
    // printf("\n>>> reached with pgid: %d <<<\n", currFgJob->cpid_1);
    waitpid(currFgJob->cpid_1, &wstatus, WNOHANG);
    if (currFgJob->pipeIndex != -1) {
        waitpid(currFgJob->cpid_2, &wstatus, WNOHANG);
    }

    currFgJob->job_status = STOPPED;

    return;
}

void sigint_handler(int num) {
    // printf("\n>>> entered SIGINT <<<\n");
    /* reap wait status */
    int wstatus;

    Job *currJob = head->nextJob;
    Job *currFgJob = (Job*)NULL;

    while (currJob != (Job*)NULL) {
        if (currJob->isBg == false && currJob->job_status == RUNNING) {
            currFgJob = currJob;
        }

        currJob = currJob->nextJob;
    }

    if (currFgJob == (Job*)NULL) {
        return;
    }

    // printf(">>> Entered SIGINT <<< \n>>> pgid: %d <<<\n", currFgJob->cpid_1);
    // currFgJob->job_status = STOPPED;
    // kill(-(currFgJob->cpid_1), SIGINT);
    kill(currFgJob->cpid_1, SIGINT);
    if (currFgJob->pipeIndex != -1) {
        kill(currFgJob->cpid_2, SIGINT);
    }
    waitpid(currFgJob->cpid_1, &wstatus, WNOHANG);
    if (currFgJob->pipeIndex != -1) {
        waitpid(currFgJob->cpid_2, &wstatus, WNOHANG);
    }
    //waitpid(-(currFgJob->cpid_1), &wstatus, WNOHANG);

    /* take the job out of the list */
    Job *before = head;
    while (before->nextJob != currFgJob) {
        before = before->nextJob;
    }
    before->nextJob = currFgJob->nextJob;
    currFgJob->nextJob = (Job*)NULL;

    /* free the job */
    freeJob(currFgJob);

    return;
}

/* split the command into tokens delimited by space and count the number of tokens */
char** parseCommand(char *cmdString, int *numTokens, int *pipeIndex) {
	// define variables used to parse the inputted command
	char **cmdStringParse = (char**)malloc(sizeof(char*) * 1);
	char *next_token;
	*numTokens = 0;
	*pipeIndex = -1;

	// obtain the first token
	next_token = strtok(cmdString, " ");

	while(next_token != NULL) {
		// add the token to cmdStringParse
		cmdStringParse[*numTokens] = next_token;

		// check for pipe
		if (strcmp(next_token, "|") == 0) {
            *pipeIndex = *numTokens;
		}

		// increment index
		(*numTokens)++;

		// allocate more space for cmdStringParse
		cmdStringParse = (char**)realloc(cmdStringParse, sizeof(char*) * (*numTokens + 1));

		// generate the next token
		next_token = strtok(NULL, " ");
	}

	// make the last element of cmdStringParse a NULL
	cmdStringParse[*numTokens] = (char*)NULL;

	// increment the number of tokens
	(*numTokens)++;

	// return the array of string commands/arguments created
	return cmdStringParse;
}

char** getCommands(char **cmd_tokenized, int size, int start) {
    char **cmd = (char**)malloc(sizeof(char*) * size);

    int index = 0;
    while (index < size - 1) {
        cmd[index] = cmd_tokenized[index + start];
        index++;
    }
    cmd[index] = (char*)NULL;

    return cmd;
}

char** initStrip(int size) {
    char **strip = (char**)malloc(sizeof(char*) * size);
    return strip;
}

void getStripCommands(char **lstrip, char **rstrip, char **left, char **right, int lsize, int rsize, Redirections *rd) {
    // printf("\n>>> leftCmdStop -> %d, rightCmdStop -> %d<<<\n>>> lsize -> %d, rsize -> %d<<<\n", rd->leftCmdStop, rd->rightCmdStop, lsize, rsize);

    if (rd->leftCmdStop != -1) {

        int index = 0;
        while (index <= rd->leftCmdStop) {
            lstrip[index] = left[index];
            index++;
        }

        int stripInd = index;
        while (stripInd < lsize) {
            lstrip[stripInd] = (char*)NULL;
            stripInd++;
        }
    } else {
        int index = 0;
        while (index < lsize) {
            lstrip[index] = left[index];
            index++;
        }
    }

    if (rsize == 0) {
        return;
    }

    if (rd->rightCmdStop != -1) {

        int index = 0;
        while (index <= rd->rightCmdStop) {
            rstrip[index] = right[index];
            index++;
        }

        int stripInd = index;
        while (stripInd < rsize) {
            rstrip[stripInd] = (char*)NULL;
            stripInd++;
        }
    } else {
        int index = 0;
        while (index < rsize) {
            rstrip[index] = right[index];
            index++;
        }
    }
}

bool getRedirections(char **orig_cmd, char **optional_cmd, Redirections *rd) {
    int index = 0;
    bool firstRedirect = false;
    while (orig_cmd[index] != (char*)NULL) {
        if (strcmp(orig_cmd[index], "<") == 0) {
            if (orig_cmd[index + 1] == (char*)NULL) {
                return false;
            }
            if (firstRedirect == false) {
                rd->leftCmdStop = index - 1;
                firstRedirect = true;
            }
            rd->leftInput = orig_cmd[index + 1];
        } else if (strcmp(orig_cmd[index], ">") == 0) {
            if (orig_cmd[index + 1] == (char*)NULL) {
                return false;
            }
            if (firstRedirect == false) {
                rd->leftCmdStop = index - 1;
                firstRedirect = true;
            }
            rd->leftOutput = orig_cmd[index + 1];
        } else if (strcmp(orig_cmd[index], "2>") == 0) {
            if (orig_cmd[index + 1] == (char*)NULL) {
                return false;
            }
            if (firstRedirect == false) {
                rd->leftCmdStop = index - 1;
                firstRedirect = true;
            }
            rd->leftError = orig_cmd[index + 1];
        }
        index++;
    }

    if (optional_cmd == (char**)NULL) {
        return true;
    }

    firstRedirect = false;
    index = 0;
    while (optional_cmd[index] != (char*)NULL) {
        if (strcmp(optional_cmd[index], "<") == 0) {
            if (optional_cmd[index + 1] == (char*)NULL) {
                return false;
            }
            if (firstRedirect == false) {
                rd->rightCmdStop = index - 1;
                firstRedirect = true;
            }
            rd->rightInput = optional_cmd[index + 1];
        } else if (strcmp(optional_cmd[index], ">") == 0) {
            if (optional_cmd[index + 1] == (char*)NULL) {
                return false;
            }
            if (firstRedirect == false) {
                rd->rightCmdStop = index - 1;
                firstRedirect = true;
            }
            rd->rightOutput = optional_cmd[index + 1];
        } else if (strcmp(optional_cmd[index], "2>") == 0) {
            if (optional_cmd[index + 1] == (char*)NULL) {
                return false;
            }
            if (firstRedirect == false) {
                rd->rightCmdStop = index - 1;
                firstRedirect = true;
            }
            rd->rightError = optional_cmd[index + 1];
        }
        index++;
    }

    return true;
}

int getJobId() {
    int highestID = 0;
    Job *curr = head->nextJob;

    while(curr != (Job*)NULL) {
        if (curr->job_id > highestID) {
            highestID = curr->job_id;
        }

        curr = curr->nextJob;
    }

    return highestID + 1;
}

Job* initJob(char *userInput, char **cmd_tokenized, char **left, char **right, char **lstrip, char **rstrip, int numTokens, int pipeIndex, bool isBg) {
    Job *job = (Job*)malloc(sizeof(Job));

    /* initialize Job attributes */
    job->userInput = userInput;
    job->cmd_tokenized = cmd_tokenized;
    job->leftPipe = left;
    job->rightPipe = right;
    job->leftStrip = lstrip;
    job->rightStrip = rstrip;
    job->numTokens = numTokens;
    job->pipeIndex = pipeIndex;
    job->cpid_1 = -1;
    job->cpid_2 = -1;
    job->isBg = isBg;
    job->execError = false;
    job->job_status = RUNNING;
    job->job_id = getJobId();

    /* add to list */
    Job *curr = head;
    while (curr != (Job*)NULL) {
        if (curr->nextJob == (Job*)NULL) {
            job->nextJob = curr->nextJob;
            curr->nextJob = job;
            break;
        }

        curr = curr->nextJob;
    }

    return job;
}

/*bool isMaxID(int id) {
    Job *curr = head->nextJob;
    int largest = -1;

    while (curr != (Job*)NULL) {
        if (largest > curr->job_id) {
            largest = curr->job_id;
        }

        curr = curr->nextJob;
    }

    if (largest == id) {
        return true;
    } else {
        return false;
    }
}*/

bool showDoneJobs() {
    Job *curr = head->nextJob;
    Job *prev;
    bool alreadyPlus = false;

    while (curr != (Job*)NULL) {
        if (curr->job_status == DONE) {
            // bool isPlus = isMaxID(curr->job_id);
            if (curr->nextJob == (Job*)NULL/*isPlus == true*/) {
                printf("[%d]+\tDone\t\t%s\n", curr->job_id, curr->userInput);
                alreadyPlus = true;
            } else {
                printf("[%d]-\tDone\t\t%s\n", curr->job_id, curr->userInput);
            }

            prev = findPrevJob(curr->cpid_1);
            Job *next = curr->nextJob;
            prev->nextJob = next;
            curr->nextJob = (Job*)NULL;
            freeJob(curr);
            curr = next;
        } else {
            curr = curr->nextJob;
        }
    }


    return alreadyPlus;
}

void execJobsCmd(bool alreadyPlus) {
    Job *currJob = head->nextJob;

    char *job_status;
    char *user_cmd;
    int job_id;
    while(currJob != (Job*)NULL) {
        if (currJob->job_status == RUNNING) {
            job_status = "Running";
        } else if (currJob->job_status == STOPPED) {
            job_status = "Stopped";
        } else if (currJob->job_status == DONE) {
            job_status = "Done";
        }

        user_cmd = currJob->userInput;
        job_id = currJob->job_id;

        //if (currJob->execError == false) {
        if (currJob->nextJob == (Job*)NULL && alreadyPlus == false) {
            printf("[%d]+\t%s\t\t%s\n", job_id, job_status, user_cmd);
        } else {
            printf("[%d]-\t%s\t\t%s\n", job_id, job_status, user_cmd);
        }
        //}

        currJob = currJob->nextJob;
    }
}

void execBgCmd() {
    Job *currJob = head->nextJob;
    Job *lastStoppedJob = (Job*)NULL;

    while(currJob != (Job*)NULL) {
        if (currJob->job_status == STOPPED) {
            lastStoppedJob = currJob;
        }

        currJob = currJob->nextJob;
    }

    /* nothing to execute because there are no stopped jobs */
    if (lastStoppedJob == (Job*)NULL) {
        return;
    }

    int wstatus;
    lastStoppedJob->job_status = RUNNING;
    lastStoppedJob->isBg = true;
    printf("[%d]+\tRunning\t\t%s &\n", lastStoppedJob->job_id, lastStoppedJob->userInput);
    //kill(-(lastStoppedJob->cpid_1), SIGCONT);
    kill(lastStoppedJob->cpid_1, SIGCONT);
    // kill(lastStoppedJob->cpid_2, SIGCONT);
    if (lastStoppedJob->pipeIndex != -1) {
        kill(lastStoppedJob->cpid_2, SIGCONT);
    }

    waitpid(lastStoppedJob->cpid_1, &wstatus, WNOHANG);
    if (lastStoppedJob->pipeIndex != -1) {
        waitpid(lastStoppedJob->cpid_2, &wstatus, WNOHANG);
    }
}

void execFgCmd() {
    Job *currJob = head;
    Job *mostRecentJob = (Job*)NULL;

    /* search for the most recent job to put in fg */
    while (currJob != (Job*)NULL) {
        if (currJob->isBg == true) {
            mostRecentJob = currJob;
        } else if (currJob->job_status == STOPPED) {
            mostRecentJob = currJob;
        }

        currJob = currJob->nextJob;
    }

    /* nothing to execute because there are no bg/stopped jobs */
    if (mostRecentJob == (Job*)NULL) {
        return;
    }

    int wstatus;
    mostRecentJob->job_status = RUNNING;
    mostRecentJob->isBg = false;
    printf("%s\n", mostRecentJob->userInput);
    //kill(-(mostRecentJob->cpid_1), SIGCONT);
    //waitpid(-(mostRecentJob->cpid_1), &wstatus, WUNTRACED);

    kill(mostRecentJob->cpid_1, SIGCONT);
    // kill(mostRecentJob->cpid_2, SIGCONT);
    if (mostRecentJob->pipeIndex != -1) {
        kill(mostRecentJob->cpid_2, SIGCONT);
    }

    waitpid(mostRecentJob->cpid_1, &wstatus, WUNTRACED);
    if (mostRecentJob->pipeIndex != -1) {
        waitpid(mostRecentJob->cpid_2, &wstatus, WUNTRACED);
    }

    if (WIFSTOPPED(wstatus) == false) {
        Job *prev = findPrevJob(mostRecentJob->cpid_1);
        if (prev != (Job*)NULL) {
            prev->nextJob = mostRecentJob->nextJob;
            mostRecentJob->nextJob = (Job*)NULL;
            freeJob(mostRecentJob);
        }
    }

    // mostRecentJob->job_status = DONE;

    /*Job *prev = findPrevJob(mostRecentJob->cpid_1);
    if (prev != (Job*)NULL) {
        prev->nextJob = mostRecentJob->nextJob;
        mostRecentJob->nextJob = (Job*)NULL;
        freeJob(mostRecentJob);
    }*/
}

int main() {
    /* variables used in YASH */
    char *input;
    char *input_dup;
    char **cmd_tokenized;
    char **leftPipe = (char**)NULL;
    char **rightPipe = (char**)NULL;
    char **leftStrip = (char**)NULL;
    char **rightStrip = (char**)NULL;
    int numTokens;
    int pipeIndex;
    bool noError;
    int s = 0;
    bool putInBackground;
    bool alreadyPlus;

    /* pipe */
    int fd_pipe[2] = {0, 0};

    /* idk if this is necessary? */
    /*int pid_yash = getpid();
    setpgid(pid_yash, pid_yash);
    tcsetpgrp(STDIN_FILENO, pid_yash);*/
    pid_yash = getpid();
    setpgid(0, 0);
    //tcsetpgrp(STDIN_FILENO, pid_yash);
    //tcsetpgrp(STDOUT_FILENO, pid_yash);

    /* initialize LinkedList of jobs */
    head = (Job*)malloc(sizeof(Job));
    head->nextJob = (Job*)NULL;

    /* signal handling */
    signal(SIGCHLD, sigchld_handler);
    signal(SIGINT, sigint_handler);
    signal(SIGTSTP, sigtstp_handler);
    //signal(SIGTTIN, SIG_IGN);
    //signal(SIGTTOU, SIG_IGN);

    // try this?
    // signal(SIGTTOU, SIG_IGN);

    // try this also?
    // signal(SIGINT, SIG_IGN);
    // signal(SIGTSTP, SIG_IGN);

    /* main while loop for YASH */
    while (input = readline("# ")) {
        alreadyPlus = showDoneJobs();

        leftPipe = (char**)NULL;
        rightPipe = (char**)NULL;
        leftStrip = (char**)NULL;
        rightStrip = (char**)NULL;

        input_dup = strdup(input);
        // free(input);
        putInBackground = false;

        cmd_tokenized = parseCommand(input, &numTokens, &pipeIndex);

        // printf("\n>>> %s <<<\n", input_dup);

        if (pipeIndex != -1) {
            leftPipe = getCommands(cmd_tokenized, pipeIndex + 1, 0);
            rightPipe = getCommands(cmd_tokenized, numTokens - pipeIndex - 1, pipeIndex + 1);
        }

        if (numTokens == 1) {
            //free(input);
            continue;
        } else if (pipeIndex == numTokens - 2) {
            //free(input);
            continue;
        }

        /* ABOVE WORKS (pretty sure?) */

        Redirections rd = {(char*)NULL, (char*)NULL, (char*)NULL, -1, (char*)NULL, (char*)NULL, (char*)NULL, -1};
        if (pipeIndex == -1) {
            noError = getRedirections(cmd_tokenized, (char**)NULL, &rd);
            if (noError == false) {
                   // free(input);
                continue;
            }
        } else {
            noError = getRedirections(leftPipe, rightPipe, &rd);
            if (noError == false) {
                   // free(input);
                continue;
            }
        }


        /* checking for special commands */
        /* bg */
        if (strcmp(cmd_tokenized[0], "bg") == 0) {
            // printf("bg\n");
            execBgCmd();
            //free(input);
            continue;
        }
        /* fg */
        if (strcmp(cmd_tokenized[0], "fg") == 0) {
            // printf("fg\n");
            execFgCmd();
           // free(input);
            continue;
        }
        /* jobs */
        if (strcmp(cmd_tokenized[0], "jobs") == 0) {
            // printf("jobs\n");
            execJobsCmd(alreadyPlus);
            // free(input);
            continue;
        }
        /* put in background */
        if (strcmp(cmd_tokenized[numTokens - 2], "&") == 0) {
            putInBackground = true;
            // printf("&\n");
            if (pipeIndex == -1) {
                cmd_tokenized[numTokens - 2] = (char*)NULL;
            } else {
                rightPipe[numTokens - pipeIndex - 3] = (char*)NULL;
            }
        }

        if (pipeIndex == -1) {
            leftStrip = initStrip(numTokens);
            getStripCommands(leftStrip, (char**)NULL, cmd_tokenized, (char**)NULL, numTokens, 0, &rd);
        } else {
            leftStrip = initStrip(pipeIndex + 1);
            rightStrip = initStrip(numTokens - pipeIndex - 1);
            getStripCommands(leftStrip, rightStrip, leftPipe, rightPipe, pipeIndex + 1, numTokens - pipeIndex - 1, &rd);
        }


        Job *job = initJob(input_dup, cmd_tokenized, leftPipe, rightPipe, leftStrip, rightStrip, numTokens, pipeIndex, putInBackground);

        /* adding jobs to list works (pretty sure) */

        if (pipeIndex == -1) {
            int cpid = fork();
            int status;
            if (cpid == 0) {
                int fd_error;
                int fd_input;
                int fd_output;

                signal(SIGCHLD, sigchld_handler);
                signal(SIGINT, sigint_handler);
                signal(SIGTSTP, sigtstp_handler);

                //setpgid(0, 0);
                //job->cpid_1 = getpid();
                // printf(">>> pid: %d <<<\n", job->cpid_1);

                if (rd.leftError != (char*)NULL) {
                    fd_error = open(rd.leftError, O_CREAT|O_WRONLY|O_TRUNC, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH);
                    if (fd_error < 0)
                        exit(-1);
                    dup2(fd_error, STDERR_FILENO);
                    close(fd_error);
                }

                if (rd.leftInput != (char*)NULL) {
                    fd_input = open(rd.leftInput, O_RDONLY/*, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH*/);
                    if (fd_input < 0)
                        exit(-1);
                    dup2(fd_input, STDIN_FILENO);
                    close(fd_input);
                }

                if (rd.leftOutput != (char*)NULL) {
                    fd_output = open(rd.leftOutput, O_CREAT|O_WRONLY|O_TRUNC, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH);
                    if (fd_output < 0)
                        exit(-1);
                    dup2(fd_output, STDOUT_FILENO);
                    close(fd_output);
                }

                // printf("%s\n", leftStrip[0]);
                execvp(leftStrip[0], leftStrip);
                // job->execError = true;
                // printf("not working\n");
                exit(-1);
            } else if (cpid > 0) {
                /*waitpid(-1, &status, 0/*WUNTRACED);
                job->cpid_1 = cpid;
                tcsetpgrp(STDIN_FILENO, pid_yash);*/

                job->cpid_1 = cpid;
                setpgid(cpid, cpid);
                // printf("cpid : %d, string: %s \n", cpid, job->leftStrip[0]);
                //tcsetpgrp(STDIN_FILENO, cpid);
                //tcsetpgrp(STDOUT_FILENO, cpid);
                if (putInBackground == true) {
                    waitpid(/*-(job->cpid_1)*/cpid, &status, WNOHANG | WUNTRACED);
                } else {
                    waitpid(/*-(job->cpid_1)*/cpid, &status, WUNTRACED);
                    // job->job_status = DONE;
                    // waitpid(rcpid, &status, 0);
                    if (WIFSTOPPED(status) == false) {
                        Job *prev = findPrevJob(cpid);
                        if (prev != (Job*)NULL) {
                            prev->nextJob = job->nextJob;
                            job->nextJob = (Job*)NULL;
                            freeJob(job);
                        }
                    }
                }
               // tcsetpgrp(STDIN_FILENO, pid_yash);
                //tcsetpgrp(STDOUT_FILENO, pid_yash);
            }
        } else {
            int lcpid;
            int rcpid;
            int status;
            pipe(fd_pipe);

            /* left child process */
            lcpid = fork();
            if (lcpid == 0) {
                int fd_error;
                int fd_input;
                int fd_output;

                signal(SIGCHLD, sigchld_handler);
                signal(SIGINT, sigint_handler);
                signal(SIGTSTP, sigtstp_handler);

                //setpgid(0, 0);
                //job->cpid_1 = getpid();
                // printf(">>> cpid1: %d <<<\n", job->cpid_1);

                close(fd_pipe[0]);
                dup2(fd_pipe[1], STDOUT_FILENO);

                if (rd.leftError != (char*)NULL) {
                    fd_error = open(rd.leftError, O_CREAT|O_WRONLY|O_TRUNC, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH);
                    if (fd_error < 0)
                        exit(-1);
                    dup2(fd_error, STDERR_FILENO);
                    close(fd_error);
                }

                if (rd.leftInput != (char*)NULL) {
                    fd_input = open(rd.leftInput, O_RDONLY/*, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH*/);
                    if (fd_input < 0)
                        exit(-1);
                    dup2(fd_input, STDIN_FILENO);
                    close(fd_input);
                }

                if (rd.leftOutput != (char*)NULL) {
                    fd_output = open(rd.leftOutput, O_CREAT|O_WRONLY|O_TRUNC, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH);
                    if (fd_output < 0)
                        exit(-1);
                    dup2(fd_output, STDOUT_FILENO);
                    close(fd_output);
                }

                execvp(leftStrip[0], leftStrip);
                // job->execError = true;
                exit(-1);
            } // else if (lcpid > 0) {

            /* right child process */
            rcpid = fork();
            if (rcpid == 0) {
                int fd_error;
                int fd_input;
                int fd_output;

                signal(SIGCHLD, sigchld_handler);
                signal(SIGINT, sigint_handler);
                signal(SIGTSTP, sigtstp_handler);

                //setpgid(0, lcpid);
                //job->cpid_2 = getpid();
                // tcsetpgrp(STDIN_FILENO, job->cpid_1);
                // printf(">>> cpid2: %d <<<\n", job->cpid_2);

                close(fd_pipe[1]);
                dup2(fd_pipe[0], STDIN_FILENO);

                if (rd.rightError != (char*)NULL) {
                    fd_error = open(rd.rightError, O_CREAT|O_WRONLY|O_TRUNC, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH);
                    if (fd_error < 0)
                        exit(-1);
                    dup2(fd_error, STDERR_FILENO);
                    close(fd_error);
                }

                if (rd.rightInput != (char*)NULL) {
                    fd_input = open(rd.rightInput, O_RDONLY/*, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH*/);
                    if (fd_input < 0)
                        exit(-1);
                    dup2(fd_input, STDIN_FILENO);
                    close(fd_input);
                }

                if (rd.rightOutput != (char*)NULL) {
                    fd_output = open(rd.rightOutput, O_CREAT|O_WRONLY|O_TRUNC, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH);
                    if (fd_output < 0)
                        exit(-1);
                    dup2(fd_output, STDOUT_FILENO);
                    close(fd_output);
                }

                execvp(rightStrip[0], rightStrip);
                // job->execError = true;
                exit(-1);
            }

            //parent process code
            /*if (lcpid > 0 && rcpid > 0) {
                waitpid(lcpid, &status, 0);
                waitpid(rcpid, &status, 0);
                // job->cpid_1 = lcpid;
                // job->cpid_2 = rcpid;
                //close(fd_pipe[0]);
                //close(fd_pipe[1]);
                tcsetpgrp(STDIN_FILENO, pid_yash);
            }*/

            if (lcpid > 0 && rcpid > 0) {
                close(fd_pipe[0]);
                close(fd_pipe[1]);
                job->cpid_1 = lcpid;
                job->cpid_2 = rcpid;
                setpgid(lcpid, lcpid);
                setpgid(rcpid, lcpid);
                //tcsetpgrp(STDIN_FILENO, lcpid);
                //tcsetpgrp(STDOUT_FILENO, lcpid);
                if (putInBackground == true) {
                    // waitpid(/*-(job->cpid_1)*/ -lcpid, &status, WUNTRACED | WNOHANG);
                    waitpid(lcpid, &status, WUNTRACED | WNOHANG);
                    waitpid(rcpid, &status, WUNTRACED | WNOHANG);
                } else {
                    //waitpid(-lcpid, &status, /*0*/WUNTRACED);
                    waitpid(lcpid, &status, WUNTRACED);
                    waitpid(rcpid, &status, WUNTRACED);
                    //job->job_status = DONE;

                    if (WIFSTOPPED(status) == false) {
                        Job *prev = findPrevJob(lcpid);
                        if (prev != (Job*)NULL) {
                            prev->nextJob = job->nextJob;
                            job->nextJob = (Job*)NULL;
                            freeJob(job);
                        }
                    }

                }

            }

        }

        // free(input);
        // printf(">>> status: %d <<<\n", s);
    }

    return 0;
}
