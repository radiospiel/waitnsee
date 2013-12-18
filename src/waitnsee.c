#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <assert.h>

static char** ARGV;
static void usage() {
  fprintf(stderr, "usage: %s path[:action] .. -- command [argument ...]\n", ARGV[0]);
  exit(1);
}

static void die(const char* s) {
  perror(s);
  exit(127);
}

#include <sys/stat.h>

/*
 * returns the mtime of a path or 0 if the path does not exist.
 */
static int mtime(const char* path) {
  struct stat buf;
  if(stat(path, &buf) < 0) {
    if(errno == ENOENT) return -1;
    die("stat");
  }
  return buf.st_mtime;
}

/*
 * The parent reruns all watches every 10 msecs or so. This constant
 * defines the time span between those runs.
 */

#define SLEEP_TIME_MICROSECS 10000

/*
 * watch is <path>[:<action>], where action is either RESTART or
 * a signal name or number. If the [:<action>] part is missing 
 * from the watch description, we default to SIGKILL.
 */

#define ACTION_RESTART -1

typedef struct {
  char* path;
  int sig;
  int recent_mtime; 
} Watch;

#define MAX_WATCHES 16

static Watch watches[MAX_WATCHES];
static unsigned int watch_count = 0;

static int action_by_name(const char* name) {
  if(!strcmp(name, "RESTART")) return ACTION_RESTART;
  
#define test(s) if(!strcmp(name, #s) || !strcmp(name, "SIG" #s)) return SIG ## s
  test(HUP);
  test(INT);
  test(KILL);
  test(TERM);
  test(USR1); 
  test(USR2);
#undef test
  return atoi(name);    
}

static void register_watch(char* watch) {
  if(watch_count == MAX_WATCHES)
    die("Too many watches defined.");
  
  Watch* p_watch = watches + (watch_count++);
  p_watch->path = watch;
  
  char* colon = strrchr(watch, ':');
  if(colon) {
    *colon++ = 0;
    p_watch->sig = action_by_name(colon);
  }
  else {
    p_watch->sig = SIGKILL;
  }

  p_watch->recent_mtime = mtime(p_watch->path);
}

void parse_and_register_watches(char** watches) {
  while(*++watches) {
    register_watch(*watches);
  }
}

/*
 * check the watch. Returns trueish if the watches mtime has changed.
 */
static int run_watch(Watch* p_watch) {
  int recent_mtime = p_watch->recent_mtime;
  p_watch->recent_mtime = mtime(p_watch->path);
  return recent_mtime != p_watch->recent_mtime;
}

/*
 * returns the first watch that changed, or NULL.
 */
static Watch* run_watches() {
  Watch* p_watch;
  for(p_watch = watches; p_watch < watches + watch_count; ++p_watch) {
    if(run_watch(p_watch)) return p_watch;
  }
  
  return 0;
}

/*
 * The child process
 */
static pid_t childpid = 0;
static char** SUBCOMMAND = 0;

static void child_process_start() {
  assert(SUBCOMMAND != 0);
  
  childpid = fork();
  if(childpid == -1)
    die("fork");

  if(childpid == 0) {
    /* This is the child. */
    execvp(*SUBCOMMAND, SUBCOMMAND);
    die(*SUBCOMMAND);
  }
}

static int child_process_exitcode() {
  assert(childpid > 0);
  
  int status;
  if(!waitpid(childpid, &status, WNOHANG)) 
    return -1;
  
  return WIFEXITED(status) ? WEXITSTATUS(status) : 0;
}

#define MAX_WAIT_MICROSECS (5 * 1000000)

static void child_process_kill(int sig) {
  assert(childpid > 0);
  
  fprintf(stderr, "signalling %d child [PID=%d]\n", sig, childpid);
  if(kill(childpid, sig)) {
    die("kill");
  }
}

static int child_process_kill_and_wait(int sig) {
  assert(childpid > 0);
  
  fprintf(stderr, "signalling %d child [PID=%d]\n", sig, childpid);
  kill(childpid, sig);
  
  int loops = 0;
  for(loops = 1; loops <= MAX_WAIT_MICROSECS / SLEEP_TIME_MICROSECS; ++loops) {
    if(loops % 100000 == 0) fprintf(stderr, ".");
    
    usleep(SLEEP_TIME_MICROSECS);

    int exitcode = child_process_exitcode();
    if(exitcode < 0) continue;
    
    fprintf(stderr, "child exited with code %d\n", exitcode);
    return exitcode;
  }

  return -1;    /* child didn't terminate. */
}

/*
 * main
 */

int main(int argc, char **argv) {
  ARGV = argv;

  /* parse arguments */
  char** watches = 0;
  
  while(*++argv) {
    if(!strcmp("--", *argv)) {
      *argv = 0;
      SUBCOMMAND = ++argv;
      break;
    }
    
    if(!watches) {
      watches = argv;
    }

    continue;
  }

  if(!watches || !SUBCOMMAND) {
    usage();
  }

  parse_and_register_watches(watches);
  
  /*
   * start child process.
   */
  child_process_start();

  /*
   * The parent reruns all tests every 10 msecs or so. If any of the
   * tests changed from the last time, we run the action.
   */

  while(1) {
    /*
     * If the child terminated we do too.
     */
    int exitcode = child_process_exitcode();
    if(exitcode >= 0)
      exit(exitcode);

    usleep(SLEEP_TIME_MICROSECS); 

    /*
     * Any change in any of the watches?
     */
    Watch* activated_watch = run_watches();
    if(!activated_watch)
      continue;
  
    switch(activated_watch->sig) {
      case ACTION_RESTART:
        child_process_kill_and_wait(SIGTERM) || child_process_kill_and_wait(SIGKILL);
        child_process_start();
        break;
      default:
        child_process_kill(activated_watch->sig);
        break;
    }
  }
  
  return 0;
}
