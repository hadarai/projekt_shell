#include "shell.h"

typedef struct proc
{
  pid_t pid;    /* process identifier */
  int state;    /* RUNNING or STOPPED or FINISHED */
  int exitcode; /* -1 if exit status not yet received */
} proc_t;

typedef struct job
{
  pid_t pgid;    /* 0 if slot is free */
  proc_t *proc;  /* array of processes running in as a job */
  int nproc;     /* number of processes */
  int state;     /* changes when live processes have same state */
  char *command; /* textual representation of command line */
} job_t;

static job_t *jobs = NULL; /* array of all jobs */
static int njobmax = 1;    /* number of slots in jobs array */
static int tty_fd = -1;    /* controlling terminal file descriptor */

static void sigchld_handler(int sig)
{
  int old_errno = errno;
  pid_t pid;
  int status;

  // TODO: Change state (FINISHED, RUNNING, STOPPED) of processes and jobs. DONE
  // Bury all children that finished, saving their status in jobs.

  while (0 < (pid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED)))
  {
    for (int i = 0; i < njobmax; i++)
    {

      if (jobs[i].nproc == 0)
      {
        jobs[i].state = FINISHED;
      }
      else
      {
        for (int j = 0; j < jobs[i].nproc; j++)
        {
          if (pid == jobs[i].proc->pid)
          {
            if (WIFEXITED(status) || WIFSIGNALED(status))
            {
              jobs[i].state = FINISHED;
              jobs[i].proc[j].exitcode = status;
            }
            if (WIFSTOPPED(status))
            {
              jobs[i].state = STOPPED;
            }
            if (WIFCONTINUED(status))
            {
              jobs[i].state = RUNNING;
            }
          }
        }
      }
    }
  }

  errno = old_errno;
}

/* When pipeline is done, its exitcode is fetched from the last process. */
static int exitcode(job_t *job)
{
  return job->proc[job->nproc - 1].exitcode;
}

static int allocjob(void)
{
  /* Find empty slot for background job. */
  for (int j = BG; j < njobmax; j++)
    if (jobs[j].pgid == 0)
      return j;

  /* If none found, allocate new one. */
  jobs = realloc(jobs, sizeof(job_t) * (njobmax + 1));
  return njobmax++;
}

static int allocproc(int j)
{
  job_t *job = &jobs[j];
  job->proc = realloc(job->proc, sizeof(proc_t) * (job->nproc + 1));
  return job->nproc++;
}

// Adds info about new job to array
int addjob(pid_t pgid, int bg)
{
  int j = bg ? allocjob() : FG;
  job_t *job = &jobs[j];
  /* Initial state of a job. */
  job->pgid = pgid;
  job->state = RUNNING;
  job->command = NULL;
  job->proc = NULL;
  job->nproc = 0;
  return j;
}

static void deljob(job_t *job)
{
  assert(job->state == FINISHED);
  free(job->command);
  free(job->proc);
  job->pgid = 0;
  job->command = NULL;
  job->proc = NULL;
  job->nproc = 0;
}

static void movejob(int from, int to)
{
  assert(jobs[to].pgid == 0);
  memcpy(&jobs[to], &jobs[from], sizeof(job_t));
  memset(&jobs[from], 0, sizeof(job_t));
}

static void mkcommand(char **cmdp, char **argv)
{
  if (*cmdp)
    strapp(cmdp, " | ");

  for (strapp(cmdp, *argv++); *argv; argv++)
  {
    strapp(cmdp, " ");
    strapp(cmdp, *argv);
  }
}

void addproc(int j, pid_t pid, char **argv)
{
  assert(j < njobmax);
  job_t *job = &jobs[j];

  int p = allocproc(j);
  proc_t *proc = &job->proc[p];
  /* Initial state of a process. */
  proc->pid = pid;
  proc->state = RUNNING;
  proc->exitcode = -1;
  mkcommand(&job->command, argv);
}

// * Returns job's state.
// * If it's finished, delete it and return exitcode through statusp.
int jobstate(int j, int *statusp)
{
  assert(j < njobmax);
  job_t *job = &jobs[j];
  int state = job->state;

  // TODO: Handle case where job has finished. DONE
  if (state == FINISHED)
  {
    statusp = job->proc[0].exitcode;
    deljob(job);
  }

  return state;
}

// Returns job's command
char *jobcmd(int j)
{
  assert(j < njobmax);
  job_t *job = &jobs[j];
  return job->command;
}

/* Continues a job that has been stopped. If move to foreground was requested,
   then move the job to foreground and start monitoring it. */
bool resumejob(int j, int bg, sigset_t *mask)
{
  if (j < 0)
  {
    for (j = njobmax - 1; j > 0 && jobs[j].state == FINISHED; j--)
      continue;
  }

  if (j >= njobmax || jobs[j].state == FINISHED)
    return false;

  // TODO: Continue stopped job. Possibly move job to foreground slot. DONE

  killpg(jobs[j].pgid, SIGCONT);
  jobs[j].state = RUNNING;

  if (bg == FG)
  {
    movejob(j, 0);
    monitorjob(mask);
  }
  return true;
}

/* Kill the job by sending it a SIGTERM. */
bool killjob(int j)
{
  if (j >= njobmax || jobs[j].state == FINISHED)
    return false;
  debug("[%d] killing '%s'\n", j, jobs[j].command);

  // TODO: I love the smell of napalm in the morning. DONE
  kill(jobs[j].pgid, SIGTERM);

  return true;
}

/* Report state of requested background jobs. Clean up finished jobs. */
void watchjobs(int which)
{
  for (int j = BG; j < njobmax; j++)
  {
    if (jobs[j].pgid == 0)
      continue;

    // TODO: Report job number, state, command and exit code or signal. DONE

    if ((which == FINISHED || which == ALL) && jobs[j].state == FINISHED)
    {
      printf("[%d]+  ", j);
      printf("FINISHED              ");
      printf("%s", jobs[j].command);
      if (WIFEXITED(jobs[j].proc[0].exitcode))
      {
        printf("        exitcode: %d\n", WEXITSTATUS(jobs[j].proc[0].exitcode));
      }
      else // signaled
      {
        printf("        signal: %d\n", WTERMSIG(jobs[j].proc[0].exitcode));
      }
      deljob(&jobs[j]);
    }
    if ((which == RUNNING || which == ALL) && jobs[j].state == RUNNING)
    {
      printf("[%d]+  ", j);
      printf("RUNNING               ");
      printf("%s\n", jobs[j].command);
    }
    if ((which == STOPPED || which == ALL) && jobs[j].state == STOPPED)
    {
      printf("[%d]+  ", j);
      printf("STOPPED               ");
      printf("%s\n", jobs[j].command);
    }
  }
}

// Monitor job execution. If it gets stopped move it to background.
// When a job has finished or has been stopped move shell to foreground.
int monitorjob(sigset_t *mask)
{
  int exitcode, state;

  // TODO: Following code requires use of Tcsetpgrp of tty_fd. DONE

  Tcsetpgrp(tty_fd, jobs[0].pgid);

  while (true)
  {
    int job_state = jobstate(0, &exitcode);
    if (job_state != RUNNING)
    {
      break;
    }
    Sigsuspend(mask);
  }
  int candidate = allocjob();
  jobs[candidate].pgid = 0;
  movejob(FG, candidate);

  Tcsetpgrp(tty_fd, getpgrp());

  return exitcode;
}

/* Called just at the beginning of shell's life. */
void initjobs(void)
{
  Signal(SIGCHLD, sigchld_handler);
  jobs = calloc(sizeof(job_t), 1);

  // Assume we're running in interactive mode, so move us to foreground.
  // Duplicate terminal fd, but do not leak it to subprocesses that execve. */
  assert(isatty(STDIN_FILENO));
  tty_fd = Dup(STDIN_FILENO);
}

/* Called just before the shell finishes. */
void shutdownjobs(void)
{
  sigset_t mask;
  Sigprocmask(SIG_BLOCK, &sigchld_mask, &mask);

  // TODO: Kill remaining jobs and wait for them to finish. DONE

  for (int j = BG; j < njobmax; j++)
  {
    int jobs_pgid = jobs[j].pgid;
    if (jobs_pgid == 0)
      continue;
    else
    {
      killjob(j);
      if (jobs[j].state != FINISHED)
      {
        Sigsuspend(&mask);
      }
    }
  }

  watchjobs(FINISHED);

  Sigprocmask(SIG_SETMASK, &mask, NULL);

  Close(tty_fd);
}
