/*
 * Copyright 2008-2016 Aerospike, Inc.
 *
 * Portions may be licensed to Aerospike, Inc. under one or more contributor
 * license agreements.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not
 * use this file except in compliance with the License. You may obtain a copy of
 * the License at http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations under
 * the License.
 */
#include <errno.h>
#include <getopt.h>

#include <aerospike/aerospike.h>
#include <aerospike/as_event.h>

#include "test.h"
#include "aerospike_test.h"

/******************************************************************************
 * MACROS
 *****************************************************************************/

#define TIMEOUT 1000
#define SCRIPT_LEN_MAX 1048576

/******************************************************************************
 * VARIABLES
 *****************************************************************************/

aerospike * as = NULL;
int g_argc = 0;
char ** g_argv = NULL;
char g_host[MAX_HOST_SIZE];
int g_port = 3000;
static char g_user[AS_USER_SIZE];
static char g_password[AS_PASSWORD_HASH_SIZE];

#if defined(AS_USE_LIBEV) || defined(AS_USE_LIBUV)
static bool g_use_async = true;
#else
static bool g_use_async = false;
#endif

/******************************************************************************
 * STATIC FUNCTIONS
 *****************************************************************************/

static bool
as_client_log_callback(as_log_level level, const char * func, const char * file, uint32_t line, const char * fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	atf_logv(stderr, as_log_level_tostring(level), ATF_LOG_PREFIX, NULL, 0, fmt, ap);
	va_end(ap);
	return true;
}

static void
usage()
{
    fprintf(stderr, "Usage: ");
    fprintf(stderr, "  -h, --host <host>\n");
    fprintf(stderr, "    The host to connect to. Default: 127.0.0.1.\n\n");
    
    fprintf(stderr, "  -p, --port <port>\n");
    fprintf(stderr, "    The port to connect to. Default: 3000.\n\n");
    
    fprintf(stderr, "  -U, --user <user>\n");
    fprintf(stderr, "    The user to connect as. Default: no user.\n\n");
    
    fprintf(stderr, "  -P[<password>], --password\n");
    fprintf(stderr, "    The user's password. If empty, a prompt is shown. Default: no password.\n\n");
    
    fprintf(stderr, "  -S, --suite <suite>\n");
    fprintf(stderr, "    The suite to be run. Default: all suites.\n\n");
    
    fprintf(stderr, "  -T, --testcase <testcase>\n");
    fprintf(stderr, "    The test case to run. Default: all test cases.\n\n");
}

static const char* short_options = "h:p:U:uP::S:T:";

static struct option long_options[] = {
	{"hosts",        1, 0, 'h'},
	{"port",         1, 0, 'p'},
	{"user",         1, 0, 'U'},
	{"password",     2, 0, 'P'},
	{"suite",        1, 0, 'S'},
	{"test",         1, 0, 'T'},
	{0,              0, 0, 0}
};

static bool parse_opts(int argc, char* argv[])
{
	int option_index = 0;
	int c;

	strcpy(g_host, "127.0.0.1");

	while ((c = getopt_long(argc, argv, short_options, long_options, &option_index)) != -1) {
		switch (c) {
		case 'h':
			if (strlen(optarg) >= sizeof(g_host)) {
				error("ERROR: host exceeds max length");
				return false;
			}
			strcpy(g_host, optarg);
			error("host:           %s", g_host);
			break;

		case 'p':
			g_port = atoi(optarg);
			break;

		case 'U':
			if (strlen(optarg) >= sizeof(g_user)) {
				error("ERROR: user exceeds max length");
				return false;
			}
			strcpy(g_user, optarg);
			error("user:           %s", g_user);
			break;

        case 'u':
            usage();
            return false;

		case 'P':
			as_password_prompt_hash(optarg, g_password);
			break;
				
		case 'S':
			// Exclude all but the specified suite from the plan.
			atf_suite_filter(optarg);
			break;
				
		case 'T':
			// Exclude all but the specified test.
			atf_test_filter(optarg);
			break;
				
		default:
	        error("unrecognized options");
            usage();
			return false;
		}
	}

	return true;
}

static bool before(atf_plan * plan) {

    if ( as ) {
        error("aerospike was already initialized");
        return false;
    }

	// Initialize logging.
	as_log_set_level(AS_LOG_LEVEL_INFO);
	as_log_set_callback(as_client_log_callback);
	
	if (g_use_async) {
		if (as_event_create_loops(1) == 0) {
			error("failed to create event loops");
			return false;
		}
	}
	
	// Initialize global lua configuration.
	as_config_lua lua;
	as_config_lua_init(&lua);
	strcpy(lua.system_path, "modules/lua-core/src");
	strcpy(lua.user_path, "src/test/lua");
	aerospike_init_lua(&lua);

	// Initialize cluster configuration.
	as_config config;
	as_config_init(&config);
	as_config_add_host(&config, g_host, g_port);
	as_config_set_user(&config, g_user, g_password);
    as_policies_init(&config.policies);

	as_error err;
	as_error_reset(&err);

	as = aerospike_new(&config);

	if ( aerospike_connect(as, &err) == AEROSPIKE_OK ) {
		debug("connected to %s:%d", g_host, g_port);
    	return true;
	}
	else {
		error("%s @ %s[%s:%d]", err.message, err.func, err.file, err.line);
		return false;
	}
}

static bool after(atf_plan * plan) {

    if ( ! as ) {
        error("aerospike was not initialized");
        return false;
    }
	
	as_error err;
	as_error_reset(&err);
	
	as_status status = aerospike_close(as, &err);
	aerospike_destroy(as);

	if (g_use_async) {
		as_event_close_loops();
	}

	if (status == AEROSPIKE_OK) {
		debug("disconnected from %s:%d", g_host, g_port);
		return true;
	}
	else {
		error("%s @ %s[%s:%d]", g_host, g_port, err.message, err.func, err.file, err.line);
		return false;
	}
}

/******************************************************************************
 * TEST PLAN
 *****************************************************************************/

PLAN(aerospike_test) {

	// This needs to be done before we add the tests.
    if (! parse_opts(g_argc, g_argv)) {
    	return;
    }
	
	plan_before(before);
	plan_after(after);

	// aerospike_key module
	plan_add(key_basics);
	plan_add(key_apply);
	plan_add(key_apply2);
	plan_add(key_operate);

	// aerospike_info module
	plan_add(info_basics);

	// aerospike_info module
	plan_add(udf_basics);
	plan_add(udf_types);
	plan_add(udf_record);

	// aerospike_sindex module
	plan_add(index_basics);

	// aerospike_query module
	plan_add(query_foreach);
	plan_add(query_background);
    plan_add(query_geospatial);

	// aerospike_scan module
	plan_add(scan_basics);

	// aerospike_scan module
	plan_add(batch_get);

	// as_policy module
	plan_add(policy_read);
	plan_add(policy_scan);

	// as_ldt module
	plan_add(ldt_lmap);

	// cdt
	plan_add(list_basics);

	if (g_use_async) {
		plan_add(key_basics_async);
		plan_add(list_basics_async);
		plan_add(key_apply_async);
		plan_add(key_pipeline);
		plan_add(batch_async);
		plan_add(scan_async);
		plan_add(query_async);
	}
}
