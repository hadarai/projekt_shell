#include <readline/readline.h>
#include <readline/history.h>

#define DEBUG 0
#include "shell.h"

sigset_t sigchld_mask;
static sigjmp_buf loop_env;

static void sigint_handler(int sig)
{
  siglongjmp(loop_env, sig);
}

// Consume all tokens related to redirection operators.
// Put opened file descriptors into inputp & output respectively.
static int do_redir(token_t *token, int ntokens, int *inputp, int *outputp)
{
  token_t mode = NULL; // T_INPUT, T_OUTPUT or NULL
  int n = 0;           // number of tokens after redirections are removed

  for (int i = 0; i < ntokens; i++)
  {
    // TODO: Handle tokens and open files as requested. DONE

    if (token[i] == T_INPUT)
    {
      *inputp = Open(token[i + 1], O_RDONLY, mode);

      token[i] = T_NULL;
      token[i + 1] = T_NULL;

      i++;
    }
    else if (token[i] == T_OUTPUT)
    {
      *outputp = Open(token[i + 1], O_WRONLY, mode);

      token[i + 1] = T_NULL;
      token[i] = T_NULL;

      i++;
    }
    else
    {
      n++;
    }
  }

  token[n] = NULL;
  return n;
}

// Execute internal command within shell's process or execute external command
// in a subprocess. External command can be run in the background.
static int do_job(token_t *token, int ntokens, bool bg)
{
  int input = -1, output = -1;
  int exitcode = 0;

  ntokens = do_redir(token, ntokens, &input, &output);
  if ((exitcode = builtin_command(token)) >= 0)
    return exitcode;

  sigset_t mask;
  Sigprocmask(SIG_BLOCK, &sigchld_mask, &mask);

  // TODO: Start a subprocess, create a job and monitor it. DONE

  pid_t child_pid = Fork();
  size_t job_index;

  if (child_pid == 0)
  {
    Sigprocmask(SIG_SETMASK, &mask, NULL);
    Setpgid(0, 0);
    Signal(SIGTSTP, SIG_DFL);

    if (input != -1)
    {
      Dup2(input, STDIN_FILENO);
    }
    if (output != -1)
    {
      Dup2(output, STDOUT_FILENO);
    }
    external_command(token);
  }
  else
  {
    job_index = addjob(child_pid, bg);
    addproc(job_index, child_pid, token);

    if (bg == FG)
    {
      monitorjob(&mask);
    }
  }
  Sigprocmask(SIG_SETMASK, &mask, NULL);
  return exitcode;
}

/* Start internal or external command in a subprocess that belongs to pipeline.
   All subprocesses in pipeline must belong to the same process group. */
static pid_t do_stage(pid_t pgid, sigset_t *mask, int input, int output,
                      token_t *token, int ntokens)
{
  ntokens = do_redir(token, ntokens, &input, &output);

  // TODO: Start a subprocess and make sure it's moved to a process group.

  pid_t child_pid = Fork();
  size_t job_index;

  if (child_pid == 0)
  {
    Sigprocmask(SIG_SETMASK, &mask, NULL);
    Setpgid(0, pgid);
    Signal(SIGTSTP, SIG_DFL);
    if (input != -1)
    {
      Dup2(input, STDIN_FILENO);
    }
    if (output != -1)
    {
      Dup2(output, STDOUT_FILENO);
    }
    token[ntokens] = T_NULL;
    external_command(token);
  }
  else
  {
  }

  return child_pid;
}

static void mkpipe(int *readp, int *writep)
{
  int fds[2];
  Pipe(fds);
  *readp = fds[0];
  *writep = fds[1];
}

// Pipeline execution creates a multiprocess job. Both internal and external
// commands are executed in subprocesses.
static int do_pipeline(token_t *token, int ntokens, bool bg)
{
  pid_t pid, pgid = 0;
  int job = -1;
  int exitcode = 0;

  int input = -1, output = -1, next_input = -1;

  mkpipe(&next_input, &output);

  sigset_t mask;
  Sigprocmask(SIG_BLOCK, &sigchld_mask, &mask);

  // TODO: Start pipeline subprocesses, create a job and monitor it.
  // Remember to close unused pipe ends!

  int number_of_segments = 1;
  for (int i = 0; i < ntokens; i++)
  {
    if (token[i] == T_PIPE)
      number_of_segments++;
  }
  int stage_length = 0;

  int now = ntokens - 1;

  for (int i = ntokens - 1; i > 0; i--)
  {
    if (token[i] == T_PIPE)
      break;
    stage_length++;
  }
  pid = do_stage(pgid, &mask, input, output, token[now], stage_length);
  mkpipe(&next_input, &output);

  now = ntokens - stage_length;
  for (size_t i = number_of_segments - 1; i > 1; i--)
  {
    now = now - stage_length;
    stage_length = 0;

    for (int j = now; j > 0; j--)
    {
      if (token[j] == T_PIPE)
        break;
      stage_length++;
    }
    now = now - stage_length + 1;
  }
  stage_length = 0;
  for (int j = 0; j < now; j++)
  {
    if (token[j] == T_PIPE)
      break;
    stage_length++;
  }
  now = now - stage_length + 1;
  printf("\n ostatni stage: %d\n", now);
  input = next_input;
  output = -1;
  pid = do_stage(pgid, &mask, input, output, token[now], stage_length);

  Sigprocmask(SIG_SETMASK, &mask, NULL);
  return exitcode;
}

static bool is_pipeline(token_t *token, int ntokens)
{
  for (int i = 0; i < ntokens; i++)
    if (token[i] == T_PIPE)
      return true;
  return false;
}

static void eval(char *cmdline)
{
  bool bg = false;
  int ntokens;
  token_t *token = tokenize(cmdline, &ntokens);

  if (ntokens > 0 && token[ntokens - 1] == T_BGJOB)
  {
    token[--ntokens] = NULL;
    bg = true;
  }

  if (ntokens > 0)
  {
    if (is_pipeline(token, ntokens))
    {
      do_pipeline(token, ntokens, bg);
    }
    else
    {
      do_job(token, ntokens, bg);
    }
  }

  free(token);
}

int main(int argc, char *argv[])
{
  rl_initialize();

  sigemptyset(&sigchld_mask);
  sigaddset(&sigchld_mask, SIGCHLD);

  initjobs();

  Signal(SIGINT, sigint_handler);
  Signal(SIGTSTP, SIG_IGN);
  Signal(SIGTTIN, SIG_IGN);
  Signal(SIGTTOU, SIG_IGN);

  char *line;
  while (true)
  {
    if (!sigsetjmp(loop_env, 1))
    {
      line = readline("# ");
    }
    else
    {
      msg("\n");
      continue;
    }

    if (line == NULL)
      break;

    if (strlen(line))
    {
      add_history(line);
      eval(line);
    }
    free(line);
    watchjobs(FINISHED);
  }

  msg("\n");
  shutdownjobs();

  return 0;
}
