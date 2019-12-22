#include "shell.h"

typedef int (*func_t)(char **argv);

typedef struct
{
  const char *name;
  func_t func;
} command_t;

static int do_quit(char **argv)
{
  shutdownjobs();
  exit(EXIT_SUCCESS);
}

/*
 * Change current working directory.
 * 'cd' - change to $HOME
 * 'cd path' - change to provided path
 */
static int do_chdir(char **argv)
{
  char *path = argv[0];
  if (path == NULL)
    path = getenv("HOME");
  int rc = chdir(path);
  if (rc < 0)
  {
    msg("cd: %s: %s\n", strerror(errno), path);
    return 1;
  }
  return 0;
}

/*
 * Displays all stopped or running jobs.
 */
static int do_jobs(char **argv)
{
  watchjobs(ALL);
  return 0;
}

/*
 * Move running or stopped background job to foreground.
 * 'fg' choose highest numbered job
 * 'fg n' choose job number n
 */
static int do_fg(char **argv)
{
  int j = argv[0] ? atoi(argv[0]) : -1;

  sigset_t mask;
  Sigprocmask(SIG_BLOCK, &sigchld_mask, &mask);
  if (!resumejob(j, FG, &mask))
    msg("fg: job not found: %s\n", argv[0]);
  Sigprocmask(SIG_SETMASK, &mask, NULL);
  return 0;
}

/*
 * Make stopped background job running.
 * 'bg' choose highest numbered job
 * 'bg n' choose job number n
 */
static int do_bg(char **argv)
{
  int j = argv[0] ? atoi(argv[0]) : -1;

  sigset_t mask;
  Sigprocmask(SIG_BLOCK, &sigchld_mask, &mask);
  if (!resumejob(j, BG, &mask))
    msg("bg: job not found: %s\n", argv[0]);
  Sigprocmask(SIG_SETMASK, &mask, NULL);
  return 0;
}

/*
 * Make stopped background job running.
 * 'bg' choose highest numbered job
 * 'bg n' choose job number n
 */
static int do_kill(char **argv)
{
  if (!argv[0])
    return -1;
  if (*argv[0] != '%')
    return -1;

  int j = atoi(argv[0] + 1);

  sigset_t mask;
  Sigprocmask(SIG_BLOCK, &sigchld_mask, &mask);
  if (!killjob(j))
    msg("kill: job not found: %s\n", argv[0]);
  Sigprocmask(SIG_SETMASK, &mask, NULL);

  return 0;
}

static command_t builtins[] = {
    {"quit", do_quit},
    {"cd", do_chdir},
    {"jobs", do_jobs},
    {"fg", do_fg},
    {"bg", do_bg},
    {"kill", do_kill},
    {NULL, NULL},
};

int builtin_command(char **argv)
{
  for (command_t *cmd = builtins; cmd->name; cmd++)
  {
    if (strcmp(argv[0], cmd->name))
      continue;
    return cmd->func(&argv[1]);
  }

  errno = ENOENT;
  return -1;
}

noreturn void external_command(char **argv)
{
  const char *path = getenv("PATH");

  if (!index(argv[0], '/') && path)
  {
    // TODO: For all paths in PATH construct an absolute path and execve it. DONE
    const char *const path_end = path + strlen(path); // dlugosc calosci
    size_t path_delimiter_position = -1;              // zmienna na pozycje dwukropka

    while (path < path_end && (path_delimiter_position = strcspn(path, ":")) > 0) //dopoki nie skonczyla mi sie sciezka i path_delimiter_position = dlugosc od początku do pozycji dwukropka (kolejnego)
    {
      char *path_directory = strndup(path, path_delimiter_position); // path_directory = kopia stringa path od 0 do path_delimiter_position

      strapp(&path_directory, "/");     // doklejam "/" na koniec zmiennej ze sciezka
      strapp(&path_directory, argv[0]); // doklejam nazwe programu na koniec zmiennej ze sciezka

      const char *const executable_name_copy = argv[0]; // kopiuje to co bylo w argumencie wykonania programu
      argv[0] = (char *)path_directory;                 // wkladamy do argumentu wskaznik na sciezke absolutna do pliku

      (void)execve(argv[0], argv, environ); // uruchamiam program z arguementu do programu

      argv[0] = (char *)executable_name_copy; // do argumentu wkladam to co bylo tam wczesniej
      free(path_directory);                   // zwalniam miejsce zajmowane przez zmienna pomocnicza (czyszcząc jej zawartosc)

      if (path_delimiter_position > 0) // jesli mi sie nie skonczyl path do ide dalej
      {
        path += path_delimiter_position + 1;
      }
    }
  }
  else
  {
    (void)execve(argv[0], argv, environ);
  }

  msg("%s: %s\n", argv[0], strerror(errno));
  exit(EXIT_FAILURE);
}
