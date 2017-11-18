/*
 * uftrace - Function (Graph) Tracer for Userspace
 *
 * Copyright (C) 2014-2017  LG Electronics, Namhyung Kim <namhyung.kim@lge.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <stdio.h>
#include <stdlib.h>
#include <argp.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>

/* This should be defined before #include "utils.h" */
#define PR_FMT "uftrace"

#include "uftrace.h"
#include "version.h"
#include "libmcount/mcount.h"
#include "libtraceevent/kbuffer.h"
#include "utils/utils.h"
#include "utils/symbol.h"
#include "utils/rbtree.h"
#include "utils/list.h"
#include "utils/fstack.h"
#include "utils/filter.h"

/* output of --version option (generated by argp runtime) */
const char *argp_program_version = "uftrace " UFTRACE_VERSION;

/* (a part of) output in --help option (generated by argp runtime) */
const char *argp_program_bug_address = "https://github.com/namhyung/uftrace/issues";

static bool dbg_domain_set = false;

enum options {
	OPT_flat	= 301,
	OPT_no_libcall,
	OPT_symbols,
	OPT_logfile,
	OPT_force,
	OPT_threads,
	OPT_no_merge,
	OPT_nop,
	OPT_time,
	OPT_max_stack,
	OPT_port,
	OPT_nopager,
	OPT_avg_total,
	OPT_avg_self,
	OPT_color,
	OPT_disabled,
	OPT_demangle,
	OPT_dbg_domain,
	OPT_report,
	OPT_column_view,
	OPT_column_offset,
	OPT_bind_not,
	OPT_task_newline,
	OPT_chrome_trace,
	OPT_flame_graph,
	OPT_sample_time,
	OPT_diff,
	OPT_sort_column,
	OPT_tid_filter,
	OPT_num_thread,
	OPT_no_comment,
	OPT_libmcount_single,
	OPT_rt_prio,
	OPT_kernel_bufsize,
	OPT_kernel_skip_out,
	OPT_kernel_full,
	OPT_kernel_only,
	OPT_list_event,
	OPT_run_cmd,
	OPT_opt_file,
	OPT_keep_pid,
	OPT_diff_policy,
	OPT_event_full,
	OPT_nest_libcall,
	OPT_record,
	OPT_auto_args,
};

static struct argp_option uftrace_options[] = {
	{ "library-path", 'L', "PATH", 0, "Load libraries from this PATH" },
	{ "filter", 'F', "FUNC", 0, "Only trace those FUNCs" },
	{ "notrace", 'N', "FUNC", 0, "Don't trace those FUNCs" },
	{ "trigger", 'T', "FUNC@act[,act,...]", 0, "Trigger action on those FUNCs" },
	{ "depth", 'D', "DEPTH", 0, "Trace functions within DEPTH" },
	{ "debug", 'v', 0, 0, "Print debug messages" },
	{ "verbose", 'v', 0, 0, "Print verbose (debug) messages" },
	{ "data", 'd', "DATA", 0, "Use this DATA instead of uftrace.data" },
	{ "flat", OPT_flat, 0, 0, "Use flat output format" },
	{ "no-libcall", OPT_no_libcall, 0, 0, "Don't trace library function calls" },
	{ "symbols", OPT_symbols, 0, 0, "Print symbol tables" },
	{ "buffer", 'b', "SIZE", 0, "Size of tracing buffer" },
	{ "logfile", OPT_logfile, "FILE", 0, "Save log messages to this file" },
	{ "force", OPT_force, 0, 0, "Trace even if executable is not instrumented" },
	{ "threads", OPT_threads, 0, 0, "Report thread stats instead" },
	{ "tid", OPT_tid_filter, "TID[,TID,...]", 0, "Only replay those tasks" },
	{ "no-merge", OPT_no_merge, 0, 0, "Don't merge leaf functions" },
	{ "nop", OPT_nop, 0, 0, "No operation (for performance test)" },
	{ "time", OPT_time, 0, 0, "Print time information" },
	{ "max-stack", OPT_max_stack, "DEPTH", 0, "Set max stack depth to DEPTH" },
	{ "kernel", 'k', 0, 0, "Trace kernel functions also (if supported)" },
	{ "host", 'H', "HOST", 0, "Send trace data to HOST instead of write to file" },
	{ "port", OPT_port, "PORT", 0, "Use PORT for network connection" },
	{ "no-pager", OPT_nopager, 0, 0, "Do not use pager" },
	{ "sort", 's', "KEY[,KEY,...]", 0, "Sort reported functions by KEYs" },
	{ "avg-total", OPT_avg_total, 0, 0, "Show average/min/max of total function time" },
	{ "avg-self", OPT_avg_self, 0, 0, "Show average/min/max of self function time" },
	{ "color", OPT_color, "SET", 0, "Use color for output: yes, no, auto" },
	{ "disable", OPT_disabled, 0, 0, "Start with tracing disabled" },
	{ "demangle", OPT_demangle, "TYPE", 0, "C++ symbol demangling: full, simple, no" },
	{ "debug-domain", OPT_dbg_domain, "DOMAIN", 0, "Filter debugging domain" },
	{ "report", OPT_report, 0, 0, "Show live report" },
	{ "column-view", OPT_column_view, 0, 0, "Print tasks in separate columns" },
	{ "column-offset", OPT_column_offset, "DEPTH", 0, "Offset of each column (default: 8)" },
	{ "no-pltbind", OPT_bind_not, 0, 0, "Do not bind dynamic symbols (LD_BIND_NOT)" },
	{ "task-newline", OPT_task_newline, 0, 0, "Interleave a newline when task is changed" },
	{ "time-filter", 't', "TIME", 0, "Hide small functions run less than the TIME" },
	{ "argument", 'A', "FUNC@arg[,arg,...]", 0, "Show function arguments" },
	{ "retval", 'R', "FUNC@retval", 0, "Show function return value" },
	{ "chrome", OPT_chrome_trace, 0, 0, "Dump recorded data in chrome trace format" },
	{ "diff", OPT_diff, "DATA", 0, "Report differences" },
	{ "sort-column", OPT_sort_column, "INDEX", 0, "Sort diff report on column INDEX" },
	{ "num-thread", OPT_num_thread, "NUM", 0, "Create NUM recorder threads" },
	{ "no-comment", OPT_no_comment, 0, 0, "Don't show comments of returned functions" },
	{ "libmcount-single", OPT_libmcount_single, 0, 0, "Use single thread version of libmcount" },
	{ "rt-prio", OPT_rt_prio, "PRIO", 0, "Record with real-time (FIFO) priority" },
	{ "kernel-depth", 'K', "DEPTH", 0, "Trace kernel functions within DEPTH" },
	{ "kernel-buffer", OPT_kernel_bufsize, "SIZE", 0, "Size of kernel tracing buffer" },
	{ "kernel-skip-out", OPT_kernel_skip_out, 0, 0, "Skip kernel functions outside of user (deprecated)" },
	{ "kernel-full", OPT_kernel_full, 0, 0, "Show kernel functions outside of user" },
	{ "kernel-only", OPT_kernel_only, 0, 0, "Dump kernel data only" },
	{ "flame-graph", OPT_flame_graph, 0, 0, "Dump recorded data in FlameGraph format" },
	{ "sample-time", OPT_sample_time, "TIME", 0, "Show flame graph with this sampling time" },
	{ "output-fields", 'f', "FIELD", 0, "Show FIELDs in the replay output (default: 'duration,tid')" },
	{ "time-range", 'r', "TIME~TIME", 0, "Show output within the TIME(timestamp or elapsed time) range only" },
	{ "patch", 'P', "FUNC", 0, "Apply dynamic patching for FUNCs" },
	{ "event", 'E', "EVENT", 0, "Enable EVENT to save more information" },
	{ "list-event", OPT_list_event, 0, 0, "List avaiable events" },
	{ "run-cmd", OPT_run_cmd, "CMDLINE", 0, "Command line that want to execute after tracing data received" },
	{ "opt-file", OPT_opt_file, "FILE", 0, "Read command-line options from FILE" },
	{ "keep-pid", OPT_keep_pid, 0, 0, "Keep same pid during execution of traced program" },
	{ "script", 'S', "SCRIPT", 0, "Run a given SCRIPT in function entry and exit" },
	{ "diff-policy", OPT_diff_policy, "POLICY", 0, "Control diff report policy" },
	{ "event-full", OPT_event_full, 0, 0, "Show all events outside of function" },
	{ "nest-libcall", OPT_nest_libcall, 0, 0, "Show nested library calls" },
	{ "record", OPT_record, 0, 0, "Record a new trace data before running command" },
	{ "auto-args", OPT_auto_args, 0, 0, "Show arguments and return value of known functions" },
	{ 0 }
};

static unsigned long parse_size(char *str)
{
	unsigned long size;
	char *unit;

	size = strtoul(str, &unit, 0);
	switch (*unit) {
	case '\0':
		break;
	case 'k':
	case 'K':
		size <<= 10;
		break;
	case 'm':
	case 'M':
		size <<= 20;
		break;
	case 'g':
	case 'G':
		size <<= 30;
		break;

	default:
		pr_use("invalid size: %s\n", str);
		size = 0;
		break;
	}

	return size;
}

static char * opt_add_string(char *old_opt, char *new_opt)
{
	return strjoin(old_opt, new_opt, ";");
}

static char * opt_add_prefix_string(char *old_opt, char *prefix, char *new_opt)
{
	new_opt = strjoin(xstrdup(prefix), new_opt, "");

	if (old_opt) {
		old_opt = strjoin(old_opt, new_opt, ";");
		free(new_opt);
		new_opt = old_opt;
	}

	return new_opt;
}

static const char * true_str[] = {
	"true", "yes", "on", "y", "1",
};

static const char * false_str[] = {
	"false", "no", "off", "n", "0",
};

static enum color_setting parse_color(char *arg)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(true_str); i++) {
		if (!strcmp(arg, true_str[i]))
			return COLOR_ON;
	}

	for (i = 0; i < ARRAY_SIZE(false_str); i++) {
		if (!strcmp(arg, false_str[i]))
			return COLOR_OFF;
	}

	if (!strcmp(arg, "auto"))
		return COLOR_AUTO;

	return COLOR_UNKNOWN;
}

static int parse_demangle(char *arg)
{
	size_t i;

	if (!strcmp(arg, "simple"))
		return DEMANGLE_SIMPLE;

	if (!strcmp(arg, "full")) {
		if (support_full_demangle())
			return DEMANGLE_FULL;
		return DEMANGLE_NOT_SUPPORTED;
	}

	for (i = 0; i < ARRAY_SIZE(false_str); i++) {
		if (!strcmp(arg, false_str[i]))
			return DEMANGLE_NONE;
	}

	return DEMANGLE_ERROR;
}

static void parse_debug_domain(char *arg)
{
	char *str, *saved_str;
	char *tok, *pos, *tmp;

	saved_str = str = xstrdup(arg);
	while ((tok = strtok_r(str, ",", &pos)) != NULL) {
		int level = -1;

		tmp = strchr(tok, ':');
		if (tmp) {
			*tmp++ = '\0';
			level = strtol(tmp, NULL, 0);
		}

		if (!strcmp(tok, "ftrace"))  /* for backward compatibility */
			dbg_domain[DBG_UFTRACE] = level;
		else if (!strcmp(tok, "uftrace"))
			dbg_domain[DBG_UFTRACE] = level;
		else if (!strcmp(tok, "symbol"))
			dbg_domain[DBG_SYMBOL] = level;
		else if (!strcmp(tok, "demangle"))
			dbg_domain[DBG_DEMANGLE] = level;
		else if (!strcmp(tok, "filter"))
			dbg_domain[DBG_FILTER] = level;
		else if (!strcmp(tok, "fstack"))
			dbg_domain[DBG_FSTACK] = level;
		else if (!strcmp(tok, "session"))
			dbg_domain[DBG_SESSION] = level;
		else if (!strcmp(tok, "kernel"))
			dbg_domain[DBG_KERNEL] = level;
		else if (!strcmp(tok, "mcount"))
			dbg_domain[DBG_MCOUNT] = level;
		else if (!strcmp(tok, "dynamic"))
			dbg_domain[DBG_DYNAMIC] = level;
		else if (!strcmp(tok, "event"))
			dbg_domain[DBG_EVENT] = level;

		str = NULL;
	}

	dbg_domain_set = true;
	free(saved_str);
}

static bool has_time_unit(const char *str)
{
	if (isalpha(str[strlen(str) - 1]))
		return true;
	else
		return false;
}

static uint64_t parse_timestamp(char *str, bool *elapsed)
{
	char *time;
	uint64_t nsec;

	if (*str == '\0')
		return 0;

	if (has_time_unit(str)) {
		*elapsed = true;
		return parse_time(str, 3);
	}

	if (asprintf(&time, "%ssec", str) < 0)
		return -1;
	nsec = parse_time(time, 9);
	free(time);
	return nsec;
}

static bool parse_time_range(struct uftrace_time_range *range, char *arg)
{
	char *str, *pos;

	str = xstrdup(arg);

	pos = strchr(str, '~');
	if (pos == NULL) {
		free(str);
		return false;
	}

	*pos++ = '\0';

	range->start = parse_timestamp(str, &range->start_elapsed);
	range->stop  = parse_timestamp(pos, &range->stop_elapsed);

	free(str);
	return true;
}

static error_t parse_option(int key, char *arg, struct argp_state *state)
{
	struct opts *opts = state->input;

	switch (key) {
	case 'L':
		opts->lib_path = arg;
		break;

	case 'F':
		opts->filter = opt_add_string(opts->filter, arg);
		break;

	case 'N':
		opts->filter = opt_add_prefix_string(opts->filter, "!", arg);
		break;

	case 'T':
		opts->trigger = opt_add_string(opts->trigger, arg);
		break;

	case 'D':
		opts->depth = strtol(arg, NULL, 0);
		if (opts->depth <= 0 || opts->depth >= OPT_DEPTH_MAX) {
			pr_use("invalid depth given: %s (ignoring..)\n", arg);
			opts->depth = OPT_DEPTH_DEFAULT;
		}
		break;

	case 'v':
		debug++;
		break;

	case 'd':
		opts->dirname = arg;
		break;

	case 'b':
		opts->bufsize = parse_size(arg);
		if (opts->bufsize & (getpagesize() - 1)) {
			pr_use("buffer size should be multiple of page size\n");
			opts->bufsize = ROUND_UP(opts->bufsize, getpagesize());
		}
		break;

	case 'k':
		opts->kernel = true;
		break;

	case 'K':
		opts->kernel = true;
		opts->kernel_depth = strtol(arg, NULL, 0);
		if (opts->kernel_depth < 1 || opts->kernel_depth > 50) {
			pr_use("invalid kernel depth: %s (ignoring...)\n", arg);
			opts->kernel_depth = 0;
		}
		break;

	case 'H':
		opts->host = arg;
		break;

	case 's':
		opts->sort_keys = opt_add_string(opts->sort_keys, arg);
		break;

	case 'S':
		opts->script_file = arg;
		break;

	case 't':
		opts->threshold = parse_time(arg, 3);
		if (opts->range.start || opts->range.stop) {
			pr_use("--time-range cannot be used with --time-filter\n");
			opts->range.start = opts->range.stop = 0;
		}
		break;

	case 'A':
		opts->args = opt_add_string(opts->args, arg);
		break;

	case 'R':
		opts->retval = opt_add_string(opts->retval, arg);
		break;

	case 'f':
		opts->fields = arg;
		break;

	case 'r':
		if (!parse_time_range(&opts->range, arg))
			pr_use("invalid time range: %s (ignoring...)\n", arg);
		if (opts->threshold) {
			pr_use("--time-filter cannot be used with --time-range\n");
			opts->threshold = 0;
		}
		break;

	case 'P':
		opts->patch = opt_add_string(opts->patch, arg);
		break;

	case 'E':
		if (!strcmp(arg, "list")) {
			pr_use("'-E list' is deprecated, use --list-event instead.\n");
			opts->list_event = true;
		}
		else
			opts->event = opt_add_string(opts->event, arg);
		break;

	case OPT_flat:
		opts->flat = true;
		break;

	case OPT_no_libcall:
		opts->libcall = false;
		break;

	case OPT_symbols:
		opts->print_symtab = true;
		break;

	case OPT_logfile:
		opts->logfile = arg;
		break;

	case OPT_force:
		opts->force = true;
		break;

	case OPT_threads:
		opts->report_thread = true;
		break;

	case OPT_tid_filter:
		opts->tid = opt_add_string(opts->tid, arg);
		break;

	case OPT_no_merge:
		opts->no_merge = true;
		break;

	case OPT_nop:
		opts->nop = true;
		break;

	case OPT_time:
		opts->time = true;
		break;

	case OPT_max_stack:
		opts->max_stack = strtol(arg, NULL, 0);
		if (opts->max_stack <= 0 || opts->max_stack > OPT_RSTACK_MAX) {
			pr_use("max stack depth should be >0 and <%d\n",
			       OPT_RSTACK_MAX);
			opts->max_stack = OPT_RSTACK_DEFAULT;
		}
		break;

	case OPT_port:
		opts->port = strtol(arg, NULL, 0);
		if (opts->port <= 0) {
			pr_use("invalid port number: %s (ignoring..)\n", arg);
			opts->port = UFTRACE_RECV_PORT;
		}
		break;

	case OPT_nopager:
		opts->use_pager = false;
		break;

	case OPT_avg_total:
		opts->avg_total = true;
		break;

	case OPT_avg_self:
		opts->avg_self = true;
		break;

	case OPT_color:
		opts->color = parse_color(arg);
		if (opts->color == COLOR_UNKNOWN) {
			pr_use("unknown color setting: %s (ignoring..)\n", arg);
			opts->color = COLOR_AUTO;
		}
		break;

	case OPT_disabled:
		opts->disabled = true;
		break;

	case OPT_demangle:
		demangler = parse_demangle(arg);
		if (demangler == DEMANGLE_ERROR) {
			pr_use("unknown demangle value: %s (ignoring..)\n", arg);
			demangler = DEMANGLE_SIMPLE;
		}
		else if (demangler == DEMANGLE_NOT_SUPPORTED) {
			pr_use("'%s' demangler is not supported\n", arg);
			demangler = DEMANGLE_SIMPLE;
		}
		break;

	case OPT_dbg_domain:
		parse_debug_domain(arg);
		break;

	case OPT_report:
		opts->report = true;
		break;

	case OPT_column_view:
		opts->column_view = true;
		break;

	case OPT_column_offset:
		opts->column_offset = strtol(arg, NULL, 0);
		break;

	case OPT_bind_not:
		opts->want_bind_not = true;
		break;

	case OPT_task_newline:
		opts->task_newline = true;
		break;

	case OPT_chrome_trace:
		opts->chrome_trace = true;
		break;

	case OPT_flame_graph:
		opts->flame_graph = true;
		break;

	case OPT_diff:
		opts->diff = arg;
		break;

	case OPT_diff_policy:
		opts->diff_policy = arg;
		break;

	case OPT_sort_column:
		opts->sort_column = strtol(arg, NULL, 0);
		if (opts->sort_column < 0 || opts->sort_column > 2) {
			pr_use("invalid column number: %d\n", opts->sort_column);
			pr_use("force to set it to --sort-column=2 for diff percentage\n");
			opts->sort_column = 2;
		}
		break;

	case OPT_num_thread:
		opts->nr_thread = strtol(arg, NULL, 0);
		if (opts->nr_thread < 0) {
			pr_use("invalid thread number: %s\n", arg);
			opts->nr_thread = 0;
		}
		break;

	case OPT_no_comment:
		opts->comment = false;
		break;

	case OPT_libmcount_single:
		opts->libmcount_single = true;
		break;

	case OPT_rt_prio:
		opts->rt_prio = strtol(arg, NULL, 0);
		if (opts->rt_prio < 1 || opts->rt_prio > 99) {
			pr_use("invalid rt prioity: %d (ignoring...)\n",
				opts->rt_prio);
			opts->rt_prio = 0;
		}
		break;

	case OPT_kernel_bufsize:
		opts->kernel_bufsize = parse_size(arg);
		if (opts->kernel_bufsize & (getpagesize() - 1)) {
			pr_use("buffer size should be multiple of page size\n");
			opts->kernel_bufsize = ROUND_UP(opts->kernel_bufsize,
							getpagesize());
		}
		break;

	case OPT_kernel_skip_out:  /* deprecated */
		opts->kernel_skip_out = true;
		break;

	case OPT_kernel_full:
		opts->kernel_skip_out = false;
		/* see setup_kernel_tracing() also */
		break;

	case OPT_kernel_only:
		opts->kernel_only = true;
		break;

	case OPT_sample_time:
		opts->sample_time = parse_time(arg, 9);
		break;

	case OPT_list_event:
		opts->list_event = true;
		break;

	case OPT_run_cmd:
		if (opts->run_cmd) {
			pr_warn("intermediate --run-cmd argument is ignored.\n");
			free_parsed_cmdline(opts->run_cmd);
		}
		opts->run_cmd = parse_cmdline(arg, NULL);
		break;

	case OPT_opt_file:
		opts->opt_file = arg;
		break;

	case OPT_keep_pid:
		opts->keep_pid = true;
		break;

	case OPT_event_full:
		opts->event_skip_out = false;
		break;

	case OPT_nest_libcall:
		/* --nest-libcall implies --force option */
		opts->force = true;
		opts->nest_libcall = true;
		break;

	case OPT_record:
		opts->record = true;
		break;

	case OPT_auto_args:
		opts->auto_args = true;
		break;

	case ARGP_KEY_ARG:
		if (state->arg_num) {
			/*
			 * This is a second non-option argument.
			 * Returning ARGP_ERR_UNKNOWN will pass control to
			 * the ARGP_KEY_ARGS case.
			 */
			return ARGP_ERR_UNKNOWN;
		}
		if (!strcmp("record", arg))
			opts->mode = UFTRACE_MODE_RECORD;
		else if (!strcmp("replay", arg))
			opts->mode = UFTRACE_MODE_REPLAY;
		else if (!strcmp("live", arg))
			opts->mode = UFTRACE_MODE_LIVE;
		else if (!strcmp("report", arg))
			opts->mode = UFTRACE_MODE_REPORT;
		else if (!strcmp("info", arg))
			opts->mode = UFTRACE_MODE_INFO;
		else if (!strcmp("recv", arg))
			opts->mode = UFTRACE_MODE_RECV;
		else if (!strcmp("dump", arg))
			opts->mode = UFTRACE_MODE_DUMP;
		else if (!strcmp("graph", arg))
			opts->mode = UFTRACE_MODE_GRAPH;
		else if (!strcmp("script", arg))
			opts->mode = UFTRACE_MODE_SCRIPT;
		else
			return ARGP_ERR_UNKNOWN; /* almost same as fall through */
		break;

	case ARGP_KEY_ARGS:
		/*
		 * process remaining non-option arguments
		 */
		if (opts->mode == UFTRACE_MODE_INVALID)
			opts->mode = UFTRACE_MODE_DEFAULT;

		opts->exename = state->argv[state->next];
		opts->idx = state->next;
		break;

	case ARGP_KEY_NO_ARGS:
	case ARGP_KEY_END:
		if (opts->opt_file)
			break;

		if (state->arg_num < 1)
			argp_usage(state);

		if (opts->exename == NULL) {
			switch (opts->mode) {
			case UFTRACE_MODE_RECORD:
			case UFTRACE_MODE_LIVE:
				argp_usage(state);
				break;
			default:
				/* will be set after read_ftrace_info() */
				break;
			}
		}
		break;

	default:
		return ARGP_ERR_UNKNOWN;
	}
	return 0;
}

static void parse_opt_file(int *argc, char ***argv, char *filename, struct opts *opts)
{
	int file_argc;
	char **file_argv;
	char *buf;
	struct stat stbuf;
	FILE *fp;
	struct argp file_argp = {
		.options = uftrace_options,
		.parser = parse_option,
		.args_doc = "[record|replay|live|report|info|dump|recv|graph|script] [<program>]",
		.doc = "uftrace -- function (graph) tracer for userspace",
	};
	char *orig_exename = NULL;
	int orig_idx = 0;

	if (stat(filename, &stbuf) < 0) {
		pr_use("Cannot use opt-file: %s: %m\n", filename);
		exit(0);
	}

	buf = xmalloc(stbuf.st_size + 1);
	fp = fopen(filename, "r");
	if (fp == NULL)
		pr_err("Open failed: %s", filename);
	fread_all(buf, stbuf.st_size, fp);
	fclose(fp);
	buf[stbuf.st_size] = '\0';

	file_argv = parse_cmdline(buf, &file_argc);

	/* clear opt_file for error reporting */
	opts->opt_file = NULL;

	if (opts->idx) {
		orig_idx = opts->idx;
		orig_exename = opts->exename;
		opts->idx = 0;
	}

	argp_parse(&file_argp, file_argc, file_argv,
		   ARGP_IN_ORDER | ARGP_PARSE_ARGV0 | ARGP_NO_ERRS,
		   NULL, opts);

	/* overwrite argv only if it's not given on command line */
	if (orig_idx == 0 && opts->idx) {
		*argc = file_argc;
		*argv = file_argv;
		/* mark it to free at the end */
		opts->opt_file = filename;
	}
	else {
		opts->idx = orig_idx;
		opts->exename = orig_exename;
		free_parsed_cmdline(file_argv);
	}

	free(buf);
}

/*
 * Parse options in a script file header.  For example,
 *
 *   # uftrace-option: -F main -A malloc@arg1
 *   def uftrace_entry():
 *     pass
 *   ...
 *
 * Note that it only handles some options like filter, trigger,
 * argument, return values and maybe some more.
 */
void parse_script_opt(struct opts *opts)
{
	FILE *fp;
	int orig_idx;
	int opt_argc;
	char **opt_argv;
	char *line = NULL;
	size_t len = 0;
	static const char optname[] = "uftrace-option";
	struct argp opt_argp = {
		.options = uftrace_options,
		.parser = parse_option,
		.args_doc = "[record|replay|live|report|info|dump|recv|graph|script] [<program>]",
		.doc = "uftrace -- function (graph) tracer for userspace",
	};

	if (opts->script_file == NULL)
		return;

	fp = fopen(opts->script_file, "r");
	if (fp == NULL)
		pr_err("cannot open script file: %s", opts->script_file);

	while (getline(&line, &len, fp) > 0) {
		char *pos;

		if (line[0] != '#')
			continue;

		pos = line + 1;
		while (isspace(*pos))
			pos++;

		if (strncmp(pos, optname, strlen(optname)))
			continue;

		/* extract option value */
		pos = strchr(line, ':');
		if (pos == NULL)
			break;

		pr_dbg("adding record option from script: %s", pos + 1);

		opt_argv = parse_cmdline(pos + 1, &opt_argc);

		orig_idx = opts->idx;
		argp_parse(&opt_argp, opt_argc, opt_argv,
			   ARGP_IN_ORDER | ARGP_PARSE_ARGV0 | ARGP_NO_ERRS,
			   NULL, opts);
		opts->idx = orig_idx;

		free_parsed_cmdline(opt_argv);
		break;
	}

	free(line);
	fclose(fp);
}

static void free_opts(struct opts *opts)
{
	free(opts->filter);
	free(opts->trigger);
	free(opts->sort_keys);
	free(opts->args);
	free(opts->retval);
	free(opts->tid);
	free(opts->event);
}

#ifndef UNIT_TEST
int main(int argc, char *argv[])
{
	struct opts opts = {
		.mode		= UFTRACE_MODE_INVALID,
		.dirname	= UFTRACE_DIR_NAME,
		.libcall	= true,
		.bufsize	= SHMEM_BUFFER_SIZE,
		.depth		= OPT_DEPTH_DEFAULT,
		.max_stack	= OPT_RSTACK_DEFAULT,
		.port		= UFTRACE_RECV_PORT,
		.use_pager	= true,
		.color		= COLOR_AUTO,  /* default to 'auto' (turn on if terminal) */
		.column_offset	= 8,
		.comment	= true,
		.kernel_skip_out= true,
		.fields         = NULL,
		.sort_column	= 2,
		.event_skip_out = true,
	};
	struct argp argp = {
		.options = uftrace_options,
		.parser = parse_option,
		.args_doc = "[record|replay|live|report|info|dump|recv|graph|script] [<program>]",
		.doc = "uftrace -- function (graph) tracer for userspace",
	};
	int ret = -1;

	/* this must be done before argp_parse() */
	logfp = stderr;
	outfp = stdout;

	argp_parse(&argp, argc, argv, ARGP_IN_ORDER, NULL, &opts);

	if (opts.opt_file)
		parse_opt_file(&argc, &argv, opts.opt_file, &opts);

	if (dbg_domain_set && !debug)
		debug = 1;

	if (opts.logfile) {
		logfp = fopen(opts.logfile, "w");
		if (logfp == NULL)
			pr_err("cannot open log file");

		setvbuf(logfp, NULL, _IOLBF, 1024);
	}
	else if (debug) {
		/* ensure normal output is not mixed by debug message */
		setvbuf(outfp, NULL, _IOLBF, 1024);
	}

	if (debug) {
		int d;

		/* set default debug level */
		for (d = 0; d < DBG_DOMAIN_MAX; d++) {
			if (dbg_domain[d] == -1 || !dbg_domain_set)
				dbg_domain[d] = debug;
		}
	}

	setup_color(opts.color);
	setup_signal();

	if (opts.mode == UFTRACE_MODE_RECORD || opts.mode == UFTRACE_MODE_RECV)
		opts.use_pager = false;
	if (opts.nop)
		opts.use_pager = false;

	if (opts.use_pager)
		start_pager();

	switch (opts.mode) {
	case UFTRACE_MODE_RECORD:
		ret = command_record(argc, argv, &opts);
		break;
	case UFTRACE_MODE_REPLAY:
		ret = command_replay(argc, argv, &opts);
		break;
	case UFTRACE_MODE_LIVE:
		ret = command_live(argc, argv, &opts);
		break;
	case UFTRACE_MODE_REPORT:
		ret = command_report(argc, argv, &opts);
		break;
	case UFTRACE_MODE_INFO:
		ret = command_info(argc, argv, &opts);
		break;
	case UFTRACE_MODE_RECV:
		ret = command_recv(argc, argv, &opts);
		break;
	case UFTRACE_MODE_DUMP:
		ret = command_dump(argc, argv, &opts);
		break;
	case UFTRACE_MODE_GRAPH:
		ret = command_graph(argc, argv, &opts);
		break;
	case UFTRACE_MODE_SCRIPT:
		ret = command_script(argc, argv, &opts);
		break;
	case UFTRACE_MODE_INVALID:
		ret = 1;
		break;
	}

	wait_for_pager();

	if (opts.logfile)
		fclose(logfp);

	if (opts.opt_file)
		free_parsed_cmdline(argv);

	free_opts(&opts);
	return ret;
}
#else
TEST_CASE(option_parsing1)
{
	char *stropt = NULL;
	int i;
	bool elapsed_time;

	TEST_EQ(parse_size("1234"), 1234);
	TEST_EQ(parse_size("10k"),  10240);
	TEST_EQ(parse_size("100M"), 100 * 1024 * 1024);

	stropt = opt_add_string(stropt, "abc");
	TEST_STREQ(stropt, "abc");
	stropt = opt_add_string(stropt, "def");
	TEST_STREQ(stropt, "abc;def");

	free(stropt);
	stropt = NULL;

	stropt = opt_add_prefix_string(stropt, "!", "abc");
	TEST_STREQ(stropt, "!abc");
	stropt = opt_add_prefix_string(stropt, "?", "def");
	TEST_STREQ(stropt, "!abc;?def");

	free(stropt);
	stropt = NULL;

	TEST_EQ(parse_color("1"),    COLOR_ON);
	TEST_EQ(parse_color("true"), COLOR_ON);
	TEST_EQ(parse_color("off"),  COLOR_OFF);
	TEST_EQ(parse_color("n"),    COLOR_OFF);
	TEST_EQ(parse_color("auto"), COLOR_AUTO);
	TEST_EQ(parse_color("ok"),   COLOR_UNKNOWN);

	TEST_EQ(parse_demangle("simple"), DEMANGLE_SIMPLE);
	TEST_EQ(parse_demangle("no"),     DEMANGLE_NONE);
	TEST_EQ(parse_demangle("0"),      DEMANGLE_NONE);
	/* full demangling might not supported */
	TEST_NE(parse_demangle("full"),   DEMANGLE_SIMPLE);

	for (i = 0; i < DBG_DOMAIN_MAX; i++)
		dbg_domain[i] = 0;

	parse_debug_domain("mcount:1,uftrace:2,symbol:3");
	TEST_EQ(dbg_domain[DBG_UFTRACE], 2);
	TEST_EQ(dbg_domain[DBG_MCOUNT],  1);
	TEST_EQ(dbg_domain[DBG_SYMBOL],  3);

	TEST_EQ(parse_timestamp("1ns", &elapsed_time), 1ULL);
	TEST_EQ(parse_timestamp("2us", &elapsed_time), 2000ULL);
	TEST_EQ(parse_timestamp("3ms", &elapsed_time), 3000000ULL);
	TEST_EQ(parse_timestamp("4s",  &elapsed_time), 4000000000ULL);
	TEST_EQ(parse_timestamp("5m",  &elapsed_time), 300000000000ULL);

	return TEST_OK;
}

TEST_CASE(option_parsing2)
{
	struct opts opts = {
		.mode = UFTRACE_MODE_INVALID,
	};
	struct argp argp = {
		.options = uftrace_options,
		.parser = parse_option,
		.args_doc = "argument description",
		.doc = "uftrace option parsing test",
	};
	char *argv[] = {
		"uftrace",
		"replay",
		"-v",
		"--data=abc.data",
		"--kernel",
		"-t", "1us",
		"-F", "foo",
		"-N", "bar",
		"-Abaz@kernel",
	};
	int argc = ARRAY_SIZE(argv);
	int saved_debug = debug;
	
	argp_parse(&argp, argc, argv, ARGP_IN_ORDER, NULL, &opts);

	TEST_EQ(opts.mode, UFTRACE_MODE_REPLAY);
	TEST_EQ(debug, saved_debug + 1);
	TEST_EQ(opts.kernel, 1);
	TEST_EQ(opts.threshold, (uint64_t)1000);
	TEST_STREQ(opts.dirname, "abc.data");
	TEST_STREQ(opts.filter, "foo;!bar");
	TEST_STREQ(opts.args, "baz@kernel");

	free_opts(&opts);
	return TEST_OK;
}

TEST_CASE(option_parsing3)
{
	struct opts opts = {
		.mode = UFTRACE_MODE_INVALID,
	};
	struct argp argp = {
		.options = uftrace_options,
		.parser = parse_option,
		.args_doc = "argument description",
		.doc = "uftrace option parsing test",
	};
	char *argv[] = { "uftrace", "-v", "--opt-file", "xxx", };
	int argc = ARRAY_SIZE(argv);
	char opt_file[] = "-K 2\n" "-b4m\n" "--column-view\n" "--depth=3\n" "t-abc";
	int file_argc;
	char **file_argv;
	FILE *fp;
	int saved_debug = debug;

	/* create opt-file */
	fp = fopen("xxx", "w");
	TEST_NE(fp, NULL);
	fwrite(opt_file, strlen(opt_file), 1, fp);
	fclose(fp);

	argp_parse(&argp, argc, argv, ARGP_IN_ORDER, NULL, &opts);
	TEST_STREQ(opts.opt_file, "xxx");

	parse_opt_file(&file_argc, &file_argv, opts.opt_file, &opts);
	TEST_EQ(file_argc, 6);

	unlink("xxx");

	TEST_EQ(opts.mode, UFTRACE_MODE_LIVE);
	TEST_EQ(debug, saved_debug + 1);
	TEST_EQ(opts.kernel, 1);
	TEST_EQ(opts.kernel_depth, 2);
	TEST_EQ(opts.depth, 3);
	TEST_EQ(opts.bufsize, 4 * 1024 * 1024);
	TEST_EQ(opts.column_view, 1);
	TEST_STREQ(opts.exename, "t-abc");

	free_parsed_cmdline(file_argv);
	free_opts(&opts);
	return TEST_OK;
}

TEST_CASE(option_parsing4)
{
	struct opts opts = {
		.mode = UFTRACE_MODE_INVALID,
	};
	struct argp argp = {
		.options = uftrace_options,
		.parser = parse_option,
		.args_doc = "argument description",
		.doc = "uftrace option parsing test",
	};
	char *argv[] = { "uftrace", "-v", "--opt-file", "xxx", };
	int argc = ARRAY_SIZE(argv);
	char opt_file[] = "-K 2\n"
		"# buffer size: 4 MB\n"
		"-b4m\n"
		"\n"
		"## show different thread with different indentation\n"
		"--column-view\n"
		"\n"
		"# limit maximum function call depth to 3\n"
		"--depth=3 # same as -D3 \n"
		"\n"
		"\n"
		"#test program\n"
		"t-abc\n"
		"\n";
	int file_argc;
	char **file_argv;
	FILE *fp;
	int saved_debug = debug;

	/* create opt-file */
	fp = fopen("xxx", "w");
	TEST_NE(fp, NULL);
	fwrite(opt_file, strlen(opt_file), 1, fp);
	fclose(fp);

	argp_parse(&argp, argc, argv, ARGP_IN_ORDER, NULL, &opts);
	TEST_STREQ(opts.opt_file, "xxx");

	parse_opt_file(&file_argc, &file_argv, opts.opt_file, &opts);
	TEST_EQ(file_argc, 6);

	unlink("xxx");

	TEST_EQ(opts.mode, UFTRACE_MODE_LIVE);
	TEST_EQ(debug, saved_debug + 1);
	TEST_EQ(opts.kernel, 1);
	TEST_EQ(opts.kernel_depth, 2);
	TEST_EQ(opts.depth, 3);
	TEST_EQ(opts.bufsize, 4 * 1024 * 1024);
	TEST_EQ(opts.column_view, 1);
	TEST_STREQ(opts.exename, "t-abc");

	free_parsed_cmdline(file_argv);
	free_opts(&opts);
	return TEST_OK;
}

TEST_CASE(option_parsing5)
{
	struct opts opts = {
		.mode = UFTRACE_MODE_INVALID,
	};
	struct argp argp = {
		.options = uftrace_options,
		.parser = parse_option,
		.args_doc = "argument description",
		.doc = "uftrace option parsing test",
	};
	char *argv[] = { "uftrace", "-v", "--opt-file", "xxx", "hello" };
	int argc = ARRAY_SIZE(argv);
	char opt_file[] = "record\n" "-F main\n" "--time-filter 1us\n" "--depth=3\n" "t-abc";
	int file_argc = argc;
	char **file_argv = argv;
	FILE *fp;
	int saved_debug = debug;

	/* create opt-file */
	fp = fopen("xxx", "w");
	TEST_NE(fp, NULL);
	fwrite(opt_file, strlen(opt_file), 1, fp);
	fclose(fp);

	argp_parse(&argp, argc, argv, ARGP_IN_ORDER, NULL, &opts);
	TEST_STREQ(opts.opt_file, "xxx");

	parse_opt_file(&file_argc, &file_argv, opts.opt_file, &opts);

	unlink("xxx");

	TEST_EQ(opts.mode, UFTRACE_MODE_RECORD);
	TEST_EQ(debug, saved_debug + 1);
	/* preserve original arg[cv] if command line is given */
	TEST_EQ(file_argc, argc);
	TEST_EQ(file_argv, (char **)argv);
	TEST_EQ(opts.threshold, (uint64_t)1000);
	TEST_EQ(opts.depth, 3);
	TEST_EQ(opts.idx, 4);
	TEST_STREQ(opts.filter, "main");
	/* it should not update exename to "t-abc" */
	TEST_STREQ(opts.exename, "hello");

	free_opts(&opts);
	return TEST_OK;
}

#endif /* UNIT_TEST */
