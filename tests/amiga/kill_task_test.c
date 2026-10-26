/*
 * Development helper: remove a stuck task by task name or CLI command name.
 * It bypasses the game's normal exit path; use it only to clear a test run
 * whose MiniGL screen has no close gadget.
 * Usage: amiga_kill_task_test <name substring>
 */
#include <exec/types.h>
#include <exec/tasks.h>
#include <exec/nodes.h>
#include <dos/dosextens.h>
#include <dos/dos.h>
#include <proto/exec.h>

#include <stdio.h>
#include <string.h>

static int BstrContains(BSTR bstr, const char *needle)
{
    const char *p;
    char name[128];
    int len;

    if (bstr == 0)
        return 0;
    p = (const char *)BADDR(bstr);
    if (p == NULL)
        return 0;
    len = (unsigned char)p[0];
    if (len >= (int)sizeof(name))
        len = (int)sizeof(name) - 1;
    memcpy(name, p + 1, (size_t)len);
    name[len] = 0;
    return strstr(name, needle) != NULL;
}

int main(int argc, char **argv)
{
    const char *needle;
    struct Task *task;

    if (argc < 2) {
        printf("usage: %s <task or command substring>\n", argv[0]);
        return 10;
    }
    needle = argv[1];
    task = FindTask(needle);
    if (task == NULL) {
        Forbid();
        for (struct Task *t = FindTask(NULL); t != NULL; t = (struct Task *)t->tc_Node.ln_Succ) {
            if (t->tc_Node.ln_Name != NULL && strstr(t->tc_Node.ln_Name, needle) != NULL) {
                task = t;
                break;
            }
            if (t->tc_Node.ln_Type == NT_PROCESS) {
                struct Process *process = (struct Process *)t;
                struct CommandLineInterface *cli = process->pr_CLI != 0
                    ? (struct CommandLineInterface *)BADDR(process->pr_CLI) : NULL;
                if (cli != NULL && BstrContains(cli->cli_CommandName, needle)) {
                    task = t;
                    break;
                }
            }
        }
        if (task == NULL) {
            printf("kill: no task matches %s\n", needle);
            for (struct Task *t = FindTask(NULL); t != NULL; t = (struct Task *)t->tc_Node.ln_Succ) {
                if (t->tc_Node.ln_Name != NULL)
                    printf("  task %lx %s\n", (unsigned long)t, t->tc_Node.ln_Name);
            }
            Permit();
            return 10;
        }
        printf("kill: removing %lx %s\n", (unsigned long)task,
               task->tc_Node.ln_Name != NULL ? task->tc_Node.ln_Name : "?");
        RemTask(task);
        Permit();
        return 0;
    }
    printf("kill: removing %lx %s\n", (unsigned long)task,
           task->tc_Node.ln_Name != NULL ? task->tc_Node.ln_Name : "?");
    Forbid();
    RemTask(task);
    Permit();
    return 0;
}
