#include <stdio.h>	/* puts(), printf(), sprintf(), snprintf() */
#include <unistd.h>	/* getopt() */
#include <stdlib.h>	/* atoi() */
#include <string.h> /* memset(), strcmp() */

#include <assert.h> /* assert() */
#include <errno.h> /* errno */

#include <pthread.h> /* pthread_self(), pthread_create() */
#include <sys/time.h> /* gettimeofday(), struct timeval, struct timespec */
#include <signal.h> /* sigprocmask(2) */
// #include <sys/signal.h> /* SIGRTMIN and others */
#include <sys/select.h> /* select(2) */
#include <stdarg.h> /* va_arg(), ... */

//#define INPUT_USESTDIO 1

#ifndef LINUX
  /* list obtained with: $ gcc -E -dM -x c /dev/null | grep -i linux */
# if linux || __linux || __linux__ || __gnu_linux__
#	define LINUX 1
# endif
#endif

#if LINUX
#	define LOCAL_SELECT_LEAVES_REMAINING_TIME_IN_TIMEOUT_PARAMETER 1
#endif

void donothing_main_end( void ) { }
void donothing_mainwait_interrupted( void ) { }

const char * g_progname;

void usage()
{
	printf(
			"%s (TODO: usage)\n"
			,
			g_progname
		);
}

void print_error_message_printf( const char * fmt, ... )
{
	char buffer[ 4 * 1024U ];
	va_list ap;
	va_start( ap, fmt );

	( void ) vsprintf( buffer, fmt, ap ); /* ignore rc */
	( void ) fprintf( stderr, "%s: ERROR: %s\n", g_progname, buffer ); /* ignore rc */

	va_end( ap );
}

/* perror() returns void, too */
void perror_from_value( int rc, const char * error_message )
{
	int errno_prev = errno;

	errno = rc;
	perror( error_message ); /* returns void */

	errno = errno_prev;
}

typedef struct
{
	unsigned int timeout_us;
	pthread_t thread_to_signal;
	int signalnumber_to_main;
	pthread_mutex_t mutex;
	pthread_cond_t cond;
} thread_function_userdata_struct_t;

void * thread_function_sendsignals( void * arg )
{
	thread_function_userdata_struct_t * userdata_ptr = ( thread_function_userdata_struct_t * ) arg;
	int rc;
	int mutex_locked = 0;
	struct timeval	tv_orig,
					tv_timeout,
					tv_triggerend;
	struct timespec abstime;

	/* FIXME: accommodate timeouts > 1 second */
	tv_timeout.tv_sec = 0;
	tv_timeout.tv_usec = userdata_ptr->timeout_us;

	while ( !0 )
	{
		if ( !( gettimeofday( &( tv_orig ), NULL ) == 0 ) )
		{
			perror( "gettimeofday()" );
			goto out;
		}
		timeradd( &( tv_orig ), &( tv_timeout ), &( tv_triggerend ) ); /* returns void */

		/* convert to nanoseconds (used by POSIX) */
		abstime.tv_sec = tv_triggerend.tv_sec;
		/* (tv_nsec declared to be 'long int' on Linux) */
		abstime.tv_nsec = ( ( unsigned long ) tv_triggerend.tv_usec ) * 1000U;

		assert( ! mutex_locked );
		if ( !( ( rc = pthread_mutex_lock( &( userdata_ptr->mutex ) ) ) == 0 ) )
		{
			perror_from_value( rc, "pthread_mutex_lock()" );
			goto out;
		}
		mutex_locked = !0;

wait_again:
		/* mutex is locked */
		rc = pthread_cond_timedwait( &( userdata_ptr->cond ), &( userdata_ptr->mutex ), &( abstime ) );
		/* mutex is locked */
		switch ( rc )
		{
			case ETIMEDOUT:
				/* ETIMEDOUT means "the condition variable has not been signalled" */
				break;
			case EINTR:
				/* maybe we've been playing with the debugger, just ignore it and wait again */
				goto wait_again;
			default:
				/* we have either signalled the condition variable, or something else has happened */
				goto out;
		}

		assert( mutex_locked );
		if ( mutex_locked )
		{
			if ( !( ( rc = pthread_mutex_unlock( &( userdata_ptr->mutex ) ) ) == 0 ) )
			{
				perror_from_value( rc, "pthread_mutex_unlock()" );
				goto out;
			}
			mutex_locked = 0;
		}

		/* MAYBE: count the number of signals we have sent */
		if ( !( ( rc = pthread_kill( userdata_ptr->thread_to_signal, userdata_ptr->signalnumber_to_main ) ) >= 0 ) )
		{
			/* maybe there are too many signal events in the recipient's "signal queue" */
			perror_from_value( rc, "pthread_kill()" );
			/* so no need to 'goto out;' here */
		}
	}

out:
	if ( mutex_locked )
	{
		if ( !( ( rc = pthread_mutex_unlock( &( userdata_ptr->mutex ) ) ) == 0 ) )
		{
			perror_from_value( rc, "pthread_mutex_unlock() (function epilog)" );
		}
	}
	return ( ( void * ) 0 );
}

char my_getchar( void )
{
	char ui_getchar;
#if INPUT_USESTDIO
	ui_getchar = getchar();
#else /* INPUT_USESTDIO */
	int rc;
	if ( !( ( rc = read( STDIN_FILENO, &( ui_getchar ), sizeof( ui_getchar ) ) ) == sizeof( ui_getchar ) ) )
	{
		ui_getchar = EOF;
	}
#endif
	return ui_getchar;
}

volatile unsigned long g_signals_caught = 0;

void signal_handler_function_signaltomain( int signal )
{
	++g_signals_caught;
}

void print_status( void )
{
	printf( "number of caught signals: %ld\n", ( unsigned long ) g_signals_caught );
}

/* signal name support {{{ */
typedef struct
{
	int l_si_signo;
	const char * l_si_name;
} signal_info_t;

/* returns NULL if an entry could not be found for the specified signal "name"
 * */
const signal_info_t * get_signal_info_from_signal_usrname( const char * src_signal_usrname )
{
	const signal_info_t * signal_info = NULL;
	const char * signal_usrname;

#	define LOCAL_ENTRY(value)	{ value, #value }
#	define LOCAL_ENTRY_PLACEHOLDER(value)	{ -1, #value }
	static signal_info_t signal_table[] =
	{
#		ifdef SIGRTMIN
			/* NOTE: the array index of this element is fixed */
			/* NOTE: the value for SIGRTMIN is *not* a constant under Linux when using the GNU C library */
			LOCAL_ENTRY_PLACEHOLDER( SIGRTMIN ),
#		endif
#		ifdef SIGUSR1
			LOCAL_ENTRY( SIGUSR1 ),
#		endif
		{ 0, NULL }
	};
#	undef LOCAL_ENTRY_PLACEHOLDER
#	undef LOCAL_ENTRY

	static int flag_signal_table_initialised = 0;

	if ( ! flag_signal_table_initialised )
	{
		unsigned signal_table_index = 0;
		const int signal_table_array_size = ( sizeof( signal_table ) / sizeof( signal_table[ 0 ] ) );

#		define LOCAL_ASSERT_ENTRY_POINTS_TO_PLACEHOLDER_ENTRY \
			do { \
				assert( signal_table_index < signal_table_array_size ); \
				assert( signal_table[ signal_table_index ].l_si_signo < 0 ); \
			} while ( 0 ) /* end */
		/* MAYBE: we could validate that the stringified value matches that in the array (strcmp()) */
#		define LOCAL_ASSIGN_ENTRY(value) \
				LOCAL_ASSERT_ENTRY_POINTS_TO_PLACEHOLDER_ENTRY; \
				signal_table[ signal_table_index ].l_si_signo = ( value ); \
				++signal_table_index; \
				/* end */

		/* finish up the static array initialisation */
#		ifdef SIGRTMIN
			LOCAL_ASSIGN_ENTRY( SIGRTMIN );
#		endif

		/* note: signal_table_index could be "pointing" to one past the end */
		/* note: currently, this validation is not needed, as the assert()s
		 * above would prevent this from failing */
		assert( signal_table_index <= signal_table_array_size );

#		undef LOCAL_ASSIGN_ENTRY
#		undef LOCAL_ASSERT_ENTRY_POINTS_TO_PLACEHOLDER_ENTRY

		flag_signal_table_initialised = !0; /* "true" */
	}

	for ( signal_info = signal_table; signal_info->l_si_signo != 0; ++signal_info )
	{
		signal_usrname = signal_info->l_si_name;
		if ( strcmp( src_signal_usrname, signal_usrname ) == 0 )
		{
			/* found it */
			/* MAYBE: move these asserts into the
			 * 'if ( ! flag_signal_table_initialised )', so that validations
			 * happen on the first function call, and don't punish later calls.
			 * (have a loop there to check every array element)
			 */
			assert( signal_info->l_si_signo > 0 );
			assert( signal_info->l_si_name != NULL );
			return signal_info;
		}
	}

	/* did not find a signal with that user name */
	return NULL;
}

#undef ENTRY_PLACEHOLDER
#undef ENTRY

/* could return a pointer to a statically allocated buffer, so the returned
 * value is good until the next call to this function */
const char * signal_usrname_to_signal_gdbname( const char * src_signal_usrname )
{
	/* should be made big enough to hold the longest signal "gdb" name + the null terminator */
	static char signal_name_buffer[ 32 ];
	/* could point to: signal_name_buffer, signal_info_t::l_si_name (from
	 * signal_table (above)), or NULL */
	const char * signal_gdbname = NULL;
	const signal_info_t * signal_info = get_signal_info_from_signal_usrname( src_signal_usrname );

	if ( signal_info != NULL )
	{
		int signal_signo = signal_info->l_si_signo;

		assert( signal_signo > 0 );

		/* NOTE: gdb seems to disallow signal numbers for values greater than
		 * the one used in the condition below */
		/* XREF: see file 'gdb/infrun.c', (sub)string "are valid as numeric signals" */
		if ( signal_signo > 15 )
		{
			/* example: 34 -> "SIG34" */
			sprintf( signal_name_buffer, "SIG%d", ( int ) signal_signo );
			signal_gdbname = signal_name_buffer;
		}
		else
		{
			signal_gdbname = signal_info->l_si_name;
		}
	}

	return signal_gdbname;
}
/* }}} */

#ifndef MILLISECONDS_PER_SECOND
#	define MILLISECONDS_PER_SECOND 1000U
#endif /* MILLISECONDS_PER_SECOND */
#ifndef MICROSECONDS_PER_MILLISECOND
#	define MICROSECONDS_PER_MILLISECOND 1000U
#endif /* MICROSECONDS_PER_MILLISECOND */
#ifndef NANOSECONDS_PER_MICROSECONDS
#	define NANOSECONDS_PER_MICROSECONDS 1000U
#endif /* NANOSECONDS_PER_MICROSECONDS */

int main( int argc, char * const * argv )
{
	int retval; /* this function's return value */
	int rc; /* last return code from int-returning functions */

	int opt; /* for getopt() */

	int flag_do_signals = !0; /* 'true' */
	int verbose = 0;
	/* ==0: use interactive move (user menu) */
	/* !=0: use that value (in microseconds) */
	unsigned main_wait_timeout_us = 0;

	sigset_t	signal_set,
				signal_set_prev; /* not used for restoring at function epilog (would need to flag that it's been used before blindly restoring) */

	pthread_t thread_handle_null = pthread_self(); /* this is the "nil" value for resource deallocation */
	pthread_t thread_handle_sendsignals = thread_handle_null;
	thread_function_userdata_struct_t thread_function_userdata_struct;

	puts( "starting program\n" );

	g_progname = argv[ 0 ];

	/* set up control structure (and save some values to be used from main(), too) */
	memset( &( thread_function_userdata_struct ), 0, sizeof( thread_function_userdata_struct ) );
	thread_function_userdata_struct.timeout_us = 10U /* milliseconds */ * MICROSECONDS_PER_MILLISECOND;
	thread_function_userdata_struct.thread_to_signal = pthread_self();
	thread_function_userdata_struct.signalnumber_to_main = SIGRTMIN;

#	define LOCAL_ERROR_ABORT( printf_like_args ) \
		do { \
			print_error_message_printf printf_like_args ; \
			goto error; \
		} while ( 0 ) /* end */

	if ( !( ( rc = pthread_mutex_init( &( thread_function_userdata_struct.mutex ), NULL ) ) == 0 ) )
	{
		perror_from_value( rc, "pthread_mutex_init()" );
		goto error;
	}
	if ( !( ( rc = pthread_cond_init( &( thread_function_userdata_struct.cond ), NULL ) ) == 0 ) )
	{
		perror_from_value( rc, "pthread_cond_init()" );
		goto error;
	}

	while ( ( opt = getopt( argc, argv, "vi:t:s:P:h" ) ) != -1 )
	{
		switch ( opt )
		{
			case 'v':
				verbose = !0;
				break;
			case 'i': /* unit: microseconds */
				thread_function_userdata_struct.timeout_us = atoi( optarg );
				break;
			case 't': /* unit: milliseconds */
				main_wait_timeout_us = atoi( optarg ) * MICROSECONDS_PER_MILLISECOND;
				break;
			case 's':
				{
					const signal_info_t * signal_info = get_signal_info_from_signal_usrname( optarg );

					if ( signal_info == NULL )
					{
						LOCAL_ERROR_ABORT(( "unrecognised signal: %s", optarg ));
					}

					thread_function_userdata_struct.signalnumber_to_main = signal_info->l_si_signo;
				}
				break;
			case 'P':
				{
					const char * signal_usrname = optarg;
					const char * signal_gdbname = signal_usrname_to_signal_gdbname( signal_usrname );
					printf( "%s\t%s\n", signal_usrname, ( signal_gdbname ? signal_gdbname : "notfound" ) );
				}
				flag_do_signals = 0; /* disable this functionality when using this cmdline option */
				break;
			case 'h':
				usage();
				goto out_success;
			default: /* '?' */
				usage();
				goto error;
		}
	}

	if ( ! flag_do_signals )
	{
		goto out_success;
	}

	if ( !( sigemptyset( &( signal_set ) ) >= 0 ) )
	{
		perror( "sigemptyset()" );
		goto error;
	}
	if ( !( sigaddset( &( signal_set ), thread_function_userdata_struct.signalnumber_to_main ) >= 0 ) )
	{
		perror( "sigaddset()" );
		goto error;
	}
#if 0
	if ( !( sigprocmask( SIG_BLOCK, &( signal_set ), &( signal_set_prev ) ) >= 0 ) )
	{
		perror( "sigprocmask()" );
		goto error;
	}
#endif

	/* set up signal handling */
	/* (does not save the previous "handler"/"disposition") */
	if ( !( signal( thread_function_userdata_struct.signalnumber_to_main, &( signal_handler_function_signaltomain ) ) != SIG_ERR ) )
	{
		perror( "signal()" );
		goto error;
	}

	/* start the thread to signal this one */
	/* note: the new thread inherits the signal process mask from the thread creating it */
	if ( !( ( rc = pthread_create( &( thread_handle_sendsignals ), NULL, &thread_function_sendsignals, &( thread_function_userdata_struct ) ) ) == 0 ) )
	{
		pthread_t thread_handle_sendsignals = thread_handle_null;
		perror_from_value( rc, "pthread_create()" );
		goto error;
	}

	/* now we can start getting signals */
	if ( !( ( rc = pthread_sigmask( SIG_UNBLOCK, &( signal_set ), NULL ) ) == 0 ) )
	{
		perror_from_value( rc, "pthread_sigmask()" );
		goto error;
	}

	/* do nothing until the user tells us to terminate the program */
	if ( main_wait_timeout_us == 0 )
	{
		char ui_getchar;
		int ui_active = !0;

		/* disabled now so that the std library functions read the whole line */
		/*  -> no need to read "left-over" data from stdin */
#if INPUT_USESTDIO && 0
		/* make sure we take one character at a time */
		if ( !( ( rc = setvbuf( stdin, NULL, _IONBF, 0 ) ) == 0 ) )
		{
			perror( "setvbuf()" );
			goto error;
		}
#endif /* INPUT_USESTDIO */

		puts( "enter: [q] to quit; [s] to print status;\n" );
		while ( ui_active )
		{
			switch ( ui_getchar = my_getchar() )
			{
				case EOF:
					fputs( "read an end-of-file from stdin. exiting.\n", stderr );
					ui_active = 0;
					continue;
				case 'q':
				case 'Q':
					fputs( "user requested to exit the program. exiting.\n", stderr );
					ui_active = 0;
					continue;

				case 's':
				case 'S':
					print_status();
					break;

				case '\n':
					continue;

				default:
					fprintf( stderr, "invalid option '%c'. try again.\n", ( char ) ui_getchar );
					break;
			}
		}

		/* consume left-over data from stdin */
#if INPUT_USESTDIO
#else /* INPUT_USESTDIO */
		{
			fd_set fdset;
			struct timeval tv;

			while ( !0 )
			{
				/* these get modified by select(2) */
				FD_ZERO( &( fdset ) ); /* returns void */
				FD_SET( STDIN_FILENO, &( fdset ) ); /* returns void */

				/* according to select(2), both fields set to zero mean "return immediately" */
				tv.tv_sec = 0;
				tv.tv_usec = 0;

				rc = select( ( STDIN_FILENO + 1 ), &( fdset ), NULL, NULL, &( tv ) );
				switch ( rc )
				{
					case 0:
						/* no more data to read */
						break;

					case 1:
						/* consume the data */
						{
							char tmp_char;
							while ( !0 )
							{
								switch ( rc = read( STDIN_FILENO, &( tmp_char ), sizeof( tmp_char ) ) )
								{
									case 0:
										/* end of file. strange */
										break;
									case sizeof( tmp_char ):
										/* normal case. carry on */
										break;
									default:
										assert( rc == -1 );
										/* maybe errno == EINTR, try again */
										continue;
								}
								break; /* leave inner loop */
							}
						}
						continue; /* read next character */

					default:
						assert( rc == -1 );
						switch ( errno )
						{
							case EINTR:
								continue; /* read next character */
							/* other errors: leave the 'switch' */
						}
						/* other errors: leave the loop (see below) */
						break;
				}
				/* if we jumped out using 'break', we need to leave the loop */
				break;
			}
		}
#endif /* INPUT_USESTDIO */
	}
	else
	{
		/* main_wait_timeout_us != 0 */

		struct timeval	tv_select,
						tv_timenow,
						tv_timeend;

		{
			const unsigned microseconds_per_second = ( MICROSECONDS_PER_MILLISECOND * MILLISECONDS_PER_SECOND );

			if ( !( gettimeofday( &( tv_timenow ), NULL ) == 0 ) )
			{
				perror( "gettimeofday()" );
				goto error;
			}
			tv_select.tv_sec = ( main_wait_timeout_us / microseconds_per_second );
			tv_select.tv_usec = ( main_wait_timeout_us % microseconds_per_second );
			timeradd( &( tv_timenow ), &( tv_select ), &( tv_timeend ) ); /* returns void */
		}

//#if ! LOCAL_SELECT_LEAVES_REMAINING_TIME_IN_TIMEOUT_PARAMETER
//#	error this code uses select(2)'s side effect: modify the 'timeout' parameter for "incomplete" waits. FIXME: modify this code to work in other operating systems.
//#endif

		while ( !0 ) /* note: 'break' condition near the end of this statement block */
		{
			/* always assume that somehow signals might not be received until the end,
			 *  so we wait until tv_timeend
			 */
			if ( !( gettimeofday( &( tv_timenow ), NULL ) == 0 ) )
			{
				perror( "gettimeofday()" );
				goto error;
			}
			/* cover the case where tv_timenow is already past the end */
			if ( !( timercmp( &( tv_timenow ), &( tv_timeend ), < ) ) )
			{
				/* we have waited enough -> leave the loop */
				break;
			}
			timersub( &( tv_timeend ), &( tv_timenow ), &( tv_select ) ); /* returns void */

			/* do the waiting in a portable way (select(2)) */
			/* note: tv_select may be changed by select(2) (for example, on LINUX) */
			if ( select( 0, NULL, NULL, NULL, &( tv_select ) ) >= 0 )
			{
				/* we have waited until the end -> leave the loop now */
				break;
			}

			/* be that EINTR or any other error, we're going to call this "do
			 * nothing" function */
			donothing_mainwait_interrupted(); /* returns void */

			/* see if we need to stop waiting */
			if ( !( gettimeofday( &( tv_timenow ), NULL ) == 0 ) )
			{
				perror( "gettimeofday()" );
				goto error;
			}
			if ( !( timercmp( &( tv_timenow ), &( tv_timeend ), < ) ) )
			{
				/* we have waited enough -> leave the loop */
				break;
			}
		}
	}

#	undef LOCAL_ERROR_ABORT

	fputs( "exiting normally (so far)\n", stderr );
out_success:
	retval = EXIT_SUCCESS;
	goto out_rc_set;

out_abort:
	fputs( "aborting program\n", stderr );
	retval = EXIT_FAILURE;
	goto out_retonly;

error:
	fputs( "exiting on error\n", stderr );
	retval = EXIT_FAILURE;
	/* falling through */
out_rc_set:
	if ( !( pthread_equal( thread_handle_sendsignals, thread_handle_null ) ) )
	{
		if ( !( ( rc = pthread_mutex_lock( &( thread_function_userdata_struct.mutex ) ) ) == 0 ) )
		{
			perror_from_value( rc, "pthread_mutex_lock()" );
			goto out_abort;
		}

		/* optional: set data for the other thread to read */

		/* signal to the other thread that it needs to end */
		if ( !( ( rc = pthread_cond_signal( &( thread_function_userdata_struct.cond ) ) ) == 0 ) )
		{
			perror_from_value( rc, "pthread_cond_signal()" );
			goto out_abort;
		}
		if ( !( ( rc = pthread_mutex_unlock( &( thread_function_userdata_struct.mutex ) ) ) == 0 ) )
		{
			perror_from_value( rc, "pthread_mutex_unlock()" );
			goto out_abort;
		}

		if ( !( ( rc = pthread_join( thread_handle_sendsignals, NULL ) ) == 0 ) )
		{
			perror_from_value( rc, "pthread_join()" );
			goto out_abort;
		}
	}
out_retonly:
	print_status();
	puts( "ending program\n" );
	donothing_main_end();
	return retval;
}

/* vim: set noexpandtab: */
/* vi: set sw=4 ts=4: */
