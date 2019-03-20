/* 
 * tsh - A tiny shell program with job control
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

/* Misc manifest constants */
#define MAXLINE    1024   /* max line size */
#define MAXARGS     128   /* max args on a command line */
#define MAXJOBS      16   /* max jobs at any point in time */
#define MAXJID    1<<16   /* max job ID */

/* Job states */
#define UNDEF 0 /* undefined */
#define FG 1    /* running in foreground */
#define BG 2    /* running in background */
#define ST 3    /* stopped */

/* 
 * Jobs states: FG (foreground), BG (background), ST (stopped)
 * Job state transitions and enabling actions:
 *     FG -> ST  : ctrl-z
 *     ST -> FG  : fg command
 *     ST -> BG  : bg command
 *     BG -> FG  : fg command
 * At most 1 job can be in the FG state.
 */

/* Global variables */
extern char **environ;      /* defined in libc */
char prompt[] = "tsh> ";    /* command line prompt (DO NOT CHANGE) */
int verbose = 0;            /* if true, print additional output */
int nextjid = 1;            /* next job ID to allocate */
char sbuf[MAXLINE];         /* for composing sprintf messages */

struct job_t {              /* The job struct */
    pid_t pid;              /* job PID */
    int jid;                /* job ID [1, 2, ...] */
    int state;              /* UNDEF, BG, FG, or ST */
    char cmdline[MAXLINE];  /* command line */
};
struct job_t jobs[MAXJOBS]; /* The job list */
/* End global variables */


/* Function prototypes */

/* Here are the functions that you will implement */
void eval(char *cmdline);
int builtin_cmd(char **argv);
void do_bgfg(char **argv);
void waitfg(pid_t pid);

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);

/* Here are helper routines that we've provided for you */
int parseline(const char *cmdline, char **argv); 
void sigquit_handler(int sig);

void clearjob(struct job_t *job);
void initjobs(struct job_t *jobs);
int maxjid(struct job_t *jobs); 
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline);
int deletejob(struct job_t *jobs, pid_t pid); 
pid_t fgpid(struct job_t *jobs);
struct job_t *getjobpid(struct job_t *jobs, pid_t pid);
struct job_t *getjobjid(struct job_t *jobs, int jid); 
int pid2jid(pid_t pid); 
void listjobs(struct job_t *jobs);

void usage(void);
void unix_error(char *msg);
void app_error(char *msg);
typedef void handler_t(int);
handler_t *Signal(int signum, handler_t *handler);

/*
 * main - The shell's main routine 
 */
int main(int argc, char **argv) 
{
    char c;
    char cmdline[MAXLINE];
    int emit_prompt = 1; /* emit prompt (default) */

    /* Redirect stderr to stdout (so that driver will get all output
     * on the pipe connected to stdout) */
    dup2(1, 2);

    /* Parse the command line */
    while ((c = getopt(argc, argv, "hvp")) != EOF) {
        switch (c) {
        case 'h':             /* print help message */
            usage();
	    break;
        case 'v':             /* emit additional diagnostic info */
            verbose = 1;
	    break;
        case 'p':             /* don't print a prompt */
            emit_prompt = 0;  /* handy for automatic testing */
	    break;
	default:
            usage();
	}
    }

    /* Install the signal handlers */

    /* These are the ones you will need to implement */
    Signal(SIGINT,  sigint_handler);   /* ctrl-c */
    Signal(SIGTSTP, sigtstp_handler);  /* ctrl-z */
    Signal(SIGCHLD, sigchld_handler);  /* Terminated or stopped child */

    /* This one provides a clean way to kill the shell */
    Signal(SIGQUIT, sigquit_handler); 

    /* Initialize the job list */
    initjobs(jobs);

    /* Execute the shell's read/eval loop */
    while (1) {

	/* Read command line */
	if (emit_prompt) {
	    printf("%s", prompt);
	    fflush(stdout);
	}
	if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin))
	    app_error("fgets error");
	if (feof(stdin)) { /* End of file (ctrl-d) */
	    fflush(stdout);
	    exit(0);
	}

	/* Evaluate the command line */
	eval(cmdline);
	fflush(stdout);
	fflush(stdout);
    } 

    exit(0); /* control never reaches here */
}
  
/* 
 * eval - Evaluate the command line that the user has just typed in
 * 
 * If the user has requested a built-in command (quit, jobs, bg or fg)
 * then execute it immediately. Otherwise, fork a child process and
 * run the job in the context of the child. If the job is running in
 * the foreground, wait for it to terminate and then return.  Note:
 * each child process must have a unique process group ID so that our
 * background children don't receive SIGINT (SIGTSTP) from the kernel
 * when we type ctrl-c (ctrl-z) at the keyboard.  
*/
void eval(char *cmdline) 

{
	char *argv[MAXARGS];		/*list of arguments*/
    	int bg; 			/* bg will get its value from parseline function whichh will decide whether the process will run in fg or bg*/
    	pid_t pid;			/*pid created for store pid of parent and child*/
    	sigset_t sset; 			/*signal set created for blocking and ublocking signals*/
    	bg = parseline(cmdline, argv);	/*Return true (bg=1) if the user has requested a BG job, false (bg=0) if the user has requested a FG job.*/
    	
	if(*argv==NULL)  		/*if command is empty then returning to main*/
    		return;
    	if(builtin_cmd(argv)) 		/*will process the command if the command is builtin else it will return to main*/
    		return;
    	else
	{
		/*as we don't want the child to be effected by SIGCHLD we use this technique to block*/ 
		if( sigemptyset(&sset) !=0 )
                	unix_error("sigemptyset gives error (in the child process)");
		 /*unix_error checks for error of these functions*/
		if(sigaddset(&sset, SIGCHLD) != 0)				/*add SIGCHLD to this signal set*/
                        unix_error("sigaddset error");
                
		if(sigprocmask(SIG_BLOCK, &sset, NULL) != 0)			/*block sset signal set till it is unblocked*/
                        unix_error("sigprocmask error");
            
                if((pid = fork()) < 0)						/*Creating a child process*/
                        unix_error("forking error");
                
	        else if(pid == 0)
 		{						/*child process*/ 
                	//if (sigprocmask(SIG_UNBLOCK, &sset, NULL) != 0)
                        //	unix_error("sigprocmask error");

                        if(setpgrp() <0)			/*new process group is set by child where child is owner*/
                                unix_error("setpgrp error");
                        
                        if(execvp(argv[0], argv) < 0) 
			{				
                                printf("%s: Command not found\n", argv[0]);
                                exit(1);
                        }
                }
        	else 
		{						/* parent will add job to joblist */
                	if(!bg)					
                                addjob(jobs, pid, FG, cmdline); /*fg process*/
                        
			else 
				 addjob(jobs, pid, BG, cmdline);/*bg process*/
                        
                        if (sigprocmask(SIG_UNBLOCK, &sset, NULL) != 0)/*unlock signal*/
			        unix_error("sigprocmask error");
                        
			if(!bg)
				waitfg(pid); /*wait for forground process to terminate*/
			
			else
				printf("[%d] (%d) %s", pid2jid(pid), pid, cmdline);
                }
        }
return ;
}  

 /* 
 * parseline - Parse the command line and build the argv array.
 * 
 * Characters enclosed in single quotes are treated as a single
 * argument.  Return true if the user has requested a BG job, false if
 * the user has requested a FG job.  
 */
int parseline(const char *cmdline, char **argv) 
{
    static char array[MAXLINE]; /* holds local copy of command line */
    char *buf = array;          /* ptr that traverses command line */
    char *delim;                /* points to first space delimiter */
    int argc;                   /* number of args */
    int bg;                     /* background job? */

    strcpy(buf, cmdline);
    buf[strlen(buf)-1] = ' ';  /* replace trailing '\n' with space */
    while (*buf && (*buf == ' ')) /* ignore leading spaces */
	buf++;

    /* Build the argv list */
    argc = 0;
    if (*buf == '\'') {
	buf++;
	delim = strchr(buf, '\'');
    }
    else {
	delim = strchr(buf, ' ');
    }

    while (delim) {
	argv[argc++] = buf;
	*delim = '\0';
	buf = delim + 1;
	while (*buf && (*buf == ' ')) /* ignore spaces */
	       buf++;

	if (*buf == '\'') {
	    buf++;
	    delim = strchr(buf, '\'');
	}
	else {
	    delim = strchr(buf, ' ');
	}
    }
    argv[argc] = NULL;
    
    if (argc == 0)  /* ignore blank line */
	return 1;

    /* should the job run in the background? */
    if ((bg = (*argv[argc-1] == '&')) != 0) {
	argv[--argc] = NULL;
    }
    return bg;
}

/* 
 * builtin_cmd - If the user has typed a built-in command then execute
 *    it immediately.  
 */
int builtin_cmd(char **argv) 
{
	if(strcmp(argv[0],"quit")==0 && argv[1]==NULL)			/*command written as quit with any arguments should not exit*/
     	{
       		int i=0;
       		for(i=0;i<MAXJOBS;i++) 					/*before quitting all the stopped processes should be exit*/
        	{ 
        		if((jobs[i].state)==3)//forthe processes which are stopped
           		{
              			printf("there are stopped jobs\n");
              			return 1;
           		}
			//else
			//	kill(jobs[i].pid,SIGQUIT);
		}	 					
 		/*if no bg or fg process stopped exit should take place*/
		
		exit(0);
	}
       	else if(strcmp(argv[0],"jobs")==0)
        { 								/*jobs list out all  the jobs*/
              listjobs(jobs);
              return 1;
       	}
       	else if(strcmp(argv[0],"fg")==0 || strcmp(argv[0],"bg")==0 )
	{								/*fg or fg is handled by do _bgfg function*/
              do_bgfg(argv);
              return 1;
       	}
       return 0;     /* not a builtin command */
}

/* 
 * do_bgfg - Execute the builtin bg and fg commands
 */
void do_bgfg(char **argv) 
{
	if(strcmp(argv[0],"fg")==0 || strcmp(argv[0],"bg" ) ==0 )
	{
		if(argv[1]==NULL) /*if pid or jid not given*/
		{
			printf("%s: command must be a PID or %%jobid argument\n",argv[0]);
			return;
		}
      		else
		{
			int l,result,count=0 ;
			int flag=0;
			l=strlen(argv[1]);
            		if(argv[1][0]=='%')		/*check either pid or jid given. if jid than flag=1 if pid than flag=0.*/
			{
				argv[1][0]='0';
				l--;
				flag=1;
			}
            		result=atoi(argv[1]);
            		while(result>0)			/*check arument is it number or not*/
			{ 
             			result=result/10;count++;
			}
             		result=atoi(argv[1]);
             		if(count!=l)			/*if not print message*/
			{
				 printf("%s: argument must be PID or %%jobid\n",argv[0]);
				 return;
			}
        		struct job_t* temp=getjobpid(jobs,result);	
			struct job_t* temp1=getjobjid(jobs,result);    
			if(flag==0)			/*if argument is pid*/
			{
                		if(temp)
				{	/*change state according to do_fgbg command*/
					printf("JOB [%d] (%d) %d\n",pid2jid(result),result,temp->state);
 		            		if(temp->state==ST && (strcmp(argv[0],"bg")==0))
					{
						temp->state=BG;
						kill(-result,SIGCONT);
                           			//printf("pid : (%d)  Changed state from ST to BG\n",result);
					}
					else if(temp->state==ST && (strcmp(argv[0],"fg")==0))
                                        {
                                                temp->state=FG;
                                                kill(-result,SIGCONT);
                                                //printf("pid : (%d)  Changed state from ST to FG\n",result);
						waitfg(result);
                                        }
					else if(temp->state==BG && (strcmp(argv[0],"fg")==0))
                                        {
                                                temp->state=FG;
                                                //printf("pid : (%d)  Changed state from BG to FG\n",result);
						waitfg(result);
                                        }
				}
				else
					printf( "%d : No such process\n ",result);    
			}
			else				/*if argument is jid*/
			{
				if(temp1)
				{	
					int result1=temp1->pid;
					//printf("JOB [%d] (%d) %d\n",result,result1,temp1->state);   
					if(temp1->state==ST && (strcmp(argv[0],"bg")==0))
                                        {       
                                                temp1->state=BG;
                                                kill(-result1,SIGCONT);
                                                //printf("jid : (%d) state changed from ST to BG\n",result);
                                        }
                                        else if(temp1->state==ST && (strcmp(argv[0],"fg")==0))
                                        {       
                                                temp1->state=FG;
                                                kill(-result1,SIGCONT);
                                                //printf("jid : (%d) state changed from ST to FG\n",result);
						waitfg(result1);
                                        }
                                        else if(temp1->state==BG && (strcmp(argv[0],"fg")==0))
                                        {       
                                                temp1->state=FG;
                                                //printf("jid : (%d) state changed from BG to FG\n",result);
						waitfg(result1);
                                        }
				}
				else
					printf( "%%%d : No such Job\n ",result); 
			}		
		}	
	}
    return;
}

/* 
 * waitfg - Block until process pid is no longer the foreground process
 */
void waitfg(pid_t pid)
{	/*sleep untill process change its state from 1 to 3 or terminate*/
	struct job_t* fgprocess=getjobpid(jobs,pid);
	if(fgprocess==NULL)
		return;
	while(fgprocess->state==1)
		sleep(1);
    return;
}

/*****************
 * Signal handlers
 *****************/

/* 
 * sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
 *     a child job terminates (becomes a zombie), or stops because it
 *     received a SIGSTOP or SIGTSTP signal. The handler reaps all
 *     available zombie children, but doesn't wait for any other
 *     currently running children to terminate.  
 */
void sigchld_handler(int sig) 
{
	int stat;
	pid_t pid;
	while((pid=waitpid(-1,&stat,WNOHANG | WUNTRACED))>0)
 	{
    
    		if(WIFSIGNALED(stat))
		{	/*if it was signaled*/
			printf("Job [%d] (%d) terminated by signal 2",pid2jid(pid),pid);
			struct job_t* temp=getjobpid(jobs,pid);
			//if(temp->state==FG)
			temp->state=UNDEF;
			deletejob(jobs,pid);	/*delete job*/
		}
		else if(WIFEXITED(stat))
		{	/*when process terminate it genarate signal*/
			//printf("\n%d: terminated normally with status:%d\n",pid,WEXITSTATUS(stat));
			struct job_t* temp=getjobpid(jobs,pid);
			//if(temp->state==FG)
			temp->state=UNDEF;
			deletejob(jobs,pid);	/*delete job*/
		}
		else if(WIFSTOPPED(stat))
		{	/*if it is stopped(^z), don't delete*/
			printf("Job [%d] (%d) stopped by signal 20\n",pid2jid(pid),pid);
			struct job_t* temp=getjobpid(jobs,pid);
			temp->state=ST;
		}
	}
   
    return;
}

/* 
 * sigint_handler - The kernel sends a SIGINT to the shell whenver the
 *    user types ctrl-c at the keyboard.  Catch it and send it along
 *    to the foreground job.  
 */
void sigint_handler(int sig) 
{    
	pid_t pid;
	if((pid=fgpid(jobs))!=0)
		kill(-pid,sig);			/*send signal for foreground job*/
	
    return;
}

/*
 * sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
 *     the user types ctrl-z at the keyboard. Catch it and suspend the
 *     foreground job by sending it a SIGTSTP.  
 */
void sigtstp_handler(int sig) 
{
	pid_t pid;
	if((pid=fgpid(jobs))!=0)
        	kill(-pid,sig);			/*send signal for foreground job*/
  	
    return;
}

/*********************
 * End signal handlers
 *********************/

/***********************************************
 * Helper routines that manipulate the job list
 **********************************************/

/* clearjob - Clear the entries in a job struct */
void clearjob(struct job_t *job) {
    job->pid = 0;
    job->jid = 0;
    job->state = UNDEF;
    job->cmdline[0] = '\0';
}

/* initjobs - Initialize the job list */
void initjobs(struct job_t *jobs) {
    int i;

    for (i = 0; i < MAXJOBS; i++)
	clearjob(&jobs[i]);
}

/* maxjid - Returns largest allocated job ID */
int maxjid(struct job_t *jobs) 
{
    int i, max=0;

    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].jid > max)
	    max = jobs[i].jid;
    return max;
}

/* addjob - Add a job to the job list */
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline) 
{
    int i;
    
    if (pid < 1)
	return 0;

    for (i = 0; i < MAXJOBS; i++) {
	if (jobs[i].pid == 0) {
	    jobs[i].pid = pid;
	    jobs[i].state = state;
	    jobs[i].jid = nextjid++;
	    if (nextjid > MAXJOBS)
		nextjid = 1;
	    strcpy(jobs[i].cmdline, cmdline);
  	    if(verbose){
	        printf("Added job [%d] %d %s\n", jobs[i].jid, jobs[i].pid, jobs[i].cmdline);
            }
            return 1;
	}
    }
    printf("Tried to create too many jobs\n");
    return 0;
}

/* deletejob - Delete a job whose PID=pid from the job list */
int deletejob(struct job_t *jobs, pid_t pid) 
{
    int i;

    if (pid < 1)
	return 0;

    for (i = 0; i < MAXJOBS; i++) {
	if (jobs[i].pid == pid) {
	    clearjob(&jobs[i]);
	    nextjid = maxjid(jobs)+1;
	    return 1;
	}
    }
    return 0;
}

/* fgpid - Return PID of current foreground job, 0 if no such job */
pid_t fgpid(struct job_t *jobs) {
    int i;

    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].state == FG)
	    return jobs[i].pid;
    return 0;
}

/* getjobpid  - Find a job (by PID) on the job list */
struct job_t *getjobpid(struct job_t *jobs, pid_t pid) {
    int i;

    if (pid < 1)
	return NULL;
    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].pid == pid)
	    return &jobs[i];
    return NULL;
}

/* getjobjid  - Find a job (by JID) on the job list */
struct job_t *getjobjid(struct job_t *jobs, int jid) 
{
    int i;

    if (jid < 1)
	return NULL;
    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].jid == jid)
	    return &jobs[i];
    return NULL;
}

/* pid2jid - Map process ID to job ID */
int pid2jid(pid_t pid) 
{
    int i;

    if (pid < 1)
	return 0;
    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].pid == pid) {
            return jobs[i].jid;
        }
    return 0;
}

/* listjobs - Print the job list */
void listjobs(struct job_t *jobs) 
{
    int i;
    
    for (i = 0; i < MAXJOBS; i++) {
	if (jobs[i].pid != 0) {
	    printf("[%d] (%d) ", jobs[i].jid, jobs[i].pid);
	    switch (jobs[i].state) {
		case BG: 
		    printf("Running ");
		    break;
		case FG: 
		    printf("Foreground ");
		    break;
		case ST: 
		    printf("Stopped ");
		    break;
	    default:
		    printf("listjobs: Internal error: job[%d].state=%d ", 
			   i, jobs[i].state);
	    }
	    printf("%s", jobs[i].cmdline);
	}
    }
}
/******************************
 * end job list helper routines
 ******************************/


/***********************
 * Other helper routines
 ***********************/

/*
 * usage - print a help message
 */
void usage(void) 
{
    printf("Usage: shell [-hvp]\n");
    printf("   -h   print this message\n");
    printf("   -v   print additional diagnostic information\n");
    printf("   -p   do not emit a command prompt\n");
    exit(1);
}

/*
 * unix_error - unix-style error routine
 */
void unix_error(char *msg)
{
    fprintf(stdout, "%s: %s\n", msg, strerror(errno));
    exit(1);
}

/*
 * app_error - application-style error routine
 */
void app_error(char *msg)
{
    fprintf(stdout, "%s\n", msg);
    exit(1);
}

/*
 * Signal - wrapper for the sigaction function
 */
handler_t *Signal(int signum, handler_t *handler) 
{
    struct sigaction action, old_action;

    action.sa_handler = handler;  
    sigemptyset(&action.sa_mask); /* block sigs of type being handled */
    action.sa_flags = SA_RESTART; /* restart syscalls if possible */

    if (sigaction(signum, &action, &old_action) < 0)
	unix_error("Signal error");
    return (old_action.sa_handler);
}

/*
 * sigquit_handler - The driver program can gracefully terminate the
 *    child shell by sending it a SIGQUIT signal.
 */
void sigquit_handler(int sig) 
{
    printf("Terminating after receipt of SIGQUIT signal\n");
    exit(1);
}
