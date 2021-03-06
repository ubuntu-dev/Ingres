/********************************************************************
**
**  Copyright (C) 1996 Ingres Corporation
**
**    Project : CA/OpenIngres Visual DBA
**
**    Source : rmcmdv.c
**    VMS "Foreign" Command-line Utility Interface
**
**    Originally written by Mark Boas (boama01)
**
**
** History:
**      21-feb-96 (musro02)
**              added dummy main procedure to further protect non-VMS builds
**      13-feb-03 (chash01) x-integrate change#461908
**              In main(), change status type from unsigned int to int.  Since
**              status receives a return from pipe(), which may be a negative
**              value, the type of status must be signed.
**      08-Jan-2010 (horda03) Bug 123119
**              In order for the REMOTE process to receive error text
**              generated by the executing command, need to pipe the
**              command's SYS$ERROR to sys$OUTPUT otherwise SYS$ERROR will 
**              default to the error channel of the RMCMD server which happens 
**              to be the RMCMDSTART.LOG file.
**      09-Feb-2010 (horda03) Bug 123119
**              A '}' was missed in 08-Jan-2010 change.
********************************************************************/

#ifdef VMS
/*
PROGRAM =       rmcmdv

NEEDLIBS =      RUNSYSLIB RUNTIMELIB FDLIB FTLIB FEDSLIB \
                UILIB LIBQSYSLIB LIBQLIB UGLIB FMTLIB AFELIB \
                LIBQGCALIB CUFLIB GCFLIB ADFLIB SQLCALIB \
                COMPATLIB 
*/
/*
	Note that MALLOCLIB should be added when this code has been CLized
*/
/** This program is designed to be used ON VMS PLATFORMS ONLY, to launch
 ** OpenIngres Front-End programs as subprocesses, either from SPAWN or
 ** C RTL 'exec' calls.  There are two reasons why this form of launching
 ** is necessary:
 ** > Ingres programs which have been linked with FRONTMAIN.OBJ must be
 **   invoked as "foreign" commands thru the DCL CLI, because FRONTMAIN
 **   actually parses program arguments from the invoking command line,
 **   and ignores (indeed, trashes) any arguments passed in the argv array.
 ** > When a program is invoked in this way (eg., thru DCL CLI), any "pipe"
 **   redirection of stdin and/or stdout is lost, along with any "open"
 **   file descriptors or channels from the launching parent.  The CLI
 **   permits specification of SYS$INPUT and SYS$OUTPUT for the command
 **   execution only.
 **
 ** This program should be invoked instead, with a normal argument list
 ** (see the VMS-specific invocation in rmcmd.sc).
 ** "argv[0]" is expected to contain the name of a foreign command CLI symbol,
 ** already defined to point to the actual image to be executed.  This
 ** routine will construct a command file to be run which contains:
 ** > redirection of SYS$INPUT and SYS$OUTPUT to the stdin/stdout "piped"
 **   locations, respectively;
 ** > a command line built from the argv array (FRONTMAIN will then process
 **   the command/program normally);
 ** > an EXIT command to terminate the spawned CLI and return the command
 **   utility's final status.
 **/

#include <descrip.h>
#include <lnmdef.h>
#include <psldef.h>
#include <string.h>
#include <stdio.h>
#include <unixio.h>
#include <stdlib.h>
#include <processes.h>
int 
main (int argc, char * argv[])
{
	int	i, clifile[2];
	int status;
	char	*p;
	char	command[256], buf[256], inpipe[256], outpipe[256], cli[256];
	$DESCRIPTOR(comdesc, command);
	$DESCRIPTOR(clidesc, cli);

/** For now, use a pipe to communicate the commands to the CLI.  Later,
 ** this could be implemented as a temporary file.
 ** Also note that "19" was the file descriptor passed from RMCMD for the
 ** SYS$OUTPUT log.
 **/
	status = pipe(clifile);
	if (status < 0) {
		sprintf(buf, "Error creating temp. command file in rmcmdv");
		write(19, buf, strlen(buf));
		fsync(19);
		return status;
		}

/** Construct a blank-delimited foreign command from input arglist:
 **/
	command[0] = 0;
	if (!argc)	return 0;
	for (i=0; i<argc; i++) {
		strcat(command, argv[i]);
		strcat(command, " ");
	}
	command[strlen(command)-1] = 0;	/* (remove trailing space) */

/** Derive names for input command file, and piped stdin and stdout devices.
 ** The command file will be specified in the SPAWN; the piped filenames
 ** will be used in logical redirection commands.
 **/
	getname(clifile[0], cli);	/* Read channel for cli command file */
	if (p=strchr(cli, (int)':'))  *(p+1) = '\0';
	clidesc.dsc$w_length = strlen(cli);
	getname(0, inpipe);	/* stdin */
	if (p=strchr(inpipe, (int)':'))  *(p+1) = '\0';
	getname(1, outpipe);	/* stdout */
	if (p=strchr(outpipe, (int)':'))  *(p+1) = '\0';

/** Write the CLI command lines to the temporary command file, and close it.
 ** (If the pipe is changed to a file, add newline chars to line ends.)
 **/
	if (strncmp(inpipe, "SYS$INPUT", 9) != 0) {
		sprintf(buf, "$ DEF/PROC SYS$INPUT %s", inpipe);
		write(clifile[1], buf, strlen(buf));
		}
	if (strncmp(outpipe, "SYS$OUTPUT", 10) != 0) {
		sprintf(buf, "$ DEF/PROC SYS$OUTPUT %s", outpipe);
		write(clifile[1], buf, strlen(buf));
		}
	if ( strncasecmp( command, "rmcmdin", 7) &&
	     strncasecmp( command, "rmcmdout", 8)  )
	{
	   sprintf(buf, "$ PIPE %s 2>SYS$OUTPUT", command);
	}
	else
	{
	   sprintf(buf, "$ %s", command);
	}

	write(clifile[1], buf, strlen(buf));
	sprintf(buf, "$ EXIT $STATUS");
	write(clifile[1], buf, strlen(buf));
	close(clifile[1]);
/* (Keep read channel of pipe open so it stays available for spawned CLI) */

/** Issue the "foreign command" invocation.
 **/
	i = LIB$SPAWN(0,		/* No command line */
			&clidesc,	/* SYS$COMMAND for CLI */
			0, 		/* default SYS$OUTPUT to parent's */
			0, 0, 0,
			&status);	/* Child's return status */
	if ((i & 1) != 1) {
		sprintf(buf, "Error invoking %s in rmcmdv; status = %08X\n",
			argv[0], i);
		write(19, buf, strlen(buf));
		fsync(19);
		close(clifile[0]);	/* (Destroy temp pipe) */
		return i;
		}
	close(clifile[0]);	/* (Destroy temp pipe) */
	return (status);
}

#else    /* (NON-VMS PLATFORMS:) */

main (){
static int x;   /* (dummy item to protect non-VMS MING builds) */
}

#endif
