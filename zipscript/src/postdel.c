#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include "../conf/zsconfig.h"
#include "../include/objects.h"
#include "../include/macros.h"
#include "../../config.h"

static struct USERINFO  **userI;
static struct GROUPINFO **groupI;
static struct VARS      raceI;
static struct LOCATIONS locations;

extern char	*c_incomplete(char *instr, char **path);

extern void readrace_file(struct LOCATIONS *locations, struct VARS *raceI, struct USERINFO **userI, struct GROUPINFO **groupI);
extern void read_write_leader_file(struct LOCATIONS *locations, struct VARS *raceI, struct USERINFO **userI);
extern unsigned long readsfv_file(struct LOCATIONS *locations, struct VARS *raceI, int getfcount);
extern char* readsfv_mysql(struct LOCATIONS *locations, struct VARS *raceI, int getfcount);
extern void remove_table_mysql(char *table);
extern void delete_sfv_mysql(struct LOCATIONS *locations);

extern short	clear_file_mysql(struct LOCATIONS *locations, char *f);
extern short	clear_file_file(struct LOCATIONS *locations, char *f); 
extern void	readrace_mysql(struct LOCATIONS *locations, struct VARS *raceI, struct USERINFO **userI, struct GROUPINFO **groupI);
extern void	read_write_leader_mysql(struct LOCATIONS *locations, struct VARS *raceI, struct USERINFO **userI);
extern short	table_exists(struct LOCATIONS *locations, char *table);
extern char*	convert(struct VARS *raceI, struct USERINFO **userI, struct GROUPINFO **groupI, char *instr);

#include "zsfunctions.h"
 
#ifdef HAVE_MYSQL
 #define data_exists(paths, datalocation) table_exists(paths, datalocation)
 #define sql_set_race   sprintf
 #define sql_set_sfv    sprintf
 #define sql_set_leader sprintf
 #define file_set_race
 #define file_set_sfv
 #define file_set_leader

 #define sql_get_index(x)	index = get_index_mysql(x)

 #define clear_file	clear_file_mysql
 #define remove_data	remove_table_mysql     
 #define readsfv	readsfv_mysql
 #define readrace	readrace_mysql
 #define delete_sfv	delete_sfv_mysql
#else
 #define data_exists(paths, datalocation) fileexists(datalocation)
 #define sql_set_sfv
 #define sql_set_race   
 #define sql_set_leader
 #define file_set_race	 sprintf
 #define file_set_sfv	 sprintf
 #define file_set_leader sprintf

 #define sql_get_index(x)

 #define remove_data	unlink
 #define readsfv	readsfv_file
 #define readrace	readrace_file
 #define delete_sfv	delete_sfv_file
 #define read_write_leader read_write_leader_file
 #define clear_file	clear_file_file
 #define connect_mysql()
 #define disconnect_mysql()
#endif


void writelog(char *msg, char *status) {
 FILE   *glfile;
 char   *date;
 char   *line, *newline;
 time_t timenow;

 if ( raceI.misc.write_log == TRUE ) {
	timenow = time(NULL);
	date = ctime(&timenow);
	glfile = fopen(log, "a+");

	line = newline = msg;
	while ( 1 ) {
		switch ( *newline++ ) {
			case 0:
				fprintf(glfile, "%.24s %s: \"%s\" \"%s\"\n", date, status, locations.path, line);
				fclose(glfile);
				return;
			case '\n':
				fprintf(glfile, "%.24s %s: \"%s\" \"%.*s\"\n", date, status, locations.path, newline - line - 1, line);
				line = newline;
				break;
			}
		}       
	}           
}


/* GET NAME OF MULTICD RELEASE (CDx/DISCx) (SYMLINK LOCATION + INCOMPLETE FILENAME)*/
void getrelname(char *directory) {
 int    cnt,
        l,
        n = 0,
        k = 2;
 long   index;
 char   *path[2];
  
 for ( cnt = strlen(directory) ; k && cnt ; cnt-- ) {
	if ( directory[cnt] == '/' ) {
		k--;
		path[k] = malloc(n + 1);
		strncpy(path[k], directory + cnt + 1, n);
		path[k][n] = 0;
   		n = 0;
		} else {
		n++;
		}
	}

 l = strlen(path[1]);

 sql_get_index(&locations);
 sql_set_race(locations.race, "R_%i", index);
 sql_set_sfv(locations.sfv, "S_%i", index);
 sql_set_leader(locations.leader, "L_%i", index);
 
 if (( ! strncasecmp(path[1], "CD"  , 2) && l <= 4 ) ||
     ( ! strncasecmp(path[1], "DISC", 4) && l <= 6 )) {  
	n = strlen(path[0]);
	raceI.misc.release_name = malloc( n + 18 );
	sprintf(raceI.misc.release_name, "%s/\\002%s\\002", path[0], path[1]);
 	locations.incomplete = c_incomplete(incomplete_cd_indicator, path);
	} else {
	raceI.misc.release_name = malloc( l + 10 );
	sprintf(raceI.misc.release_name, "\\002%s\\002", path[1]);
	locations.incomplete = c_incomplete(incomplete_indicator, path);
	}
 free(path[1]);
 free(path[0]);
}



unsigned char get_filetype(char *ext) {

 if ( ! memcmp(ext, "sfv", 4)) return 1;
 if ( ! clear_file(&locations, raceI.file.name)) return 4;
 if ( ! memcmp(ext, "zip", 4)) return 0;
 if ( ! memcmp(ext, "nfo", 4)) return 2;
 if ( ! strcomp(ignored_types, ext)) return 3;

 return 255;
}



int main( int argc, char **argv ) {
 char		*fileext, *name_p, *temp_p;
 char		*target;
 int		n;
 unsigned char	empty_dir = 0;
 unsigned char	incomplete = 0;

 if (argc == 1) {
   d_log("no param\n");
   return 0;
 }

#if ( program_uid > 0 )
 d_log("Trying to change effective gid\n");
 setegid(program_gid);
 d_log("Trying to change effective uid\n");
 seteuid(program_uid);
#endif

 d_log("Reading directory structure\n");
 rescandir();

 if ( fileexists(argv[1] + 5) ) {
	d_log("File still exists\n");
	return 0;
	}

 connect_mysql();
 umask(0666 & 000);

 d_log("Clearing arrays\n");
 bzero(&raceI.total, sizeof(struct race_total));
 raceI.misc.slowest_user[0] = 30000;
 raceI.misc.fastest_user[0] = 0;

 raceI.user.name = malloc(25);
 d_log("Reading user name from env\n");
 strncpy(raceI.user.name, getenv("USER"), 25);
 d_log("Reading group name from env\n");
 strncpy(raceI.user.group, getenv("GROUP"), 25);

 if ( ! *raceI.user.group ) {
	memcpy(raceI.user.group, "NoGroup", 8);
	}

 d_log("Allocating memory for variables\n");
 userI	= malloc(sizeof(struct USERINFO *) * 30);
 groupI	= malloc(sizeof(struct GROUPINFO *) * 30);

 locations.path   = malloc(PATH_MAX);
 getcwd(locations.path, PATH_MAX);
 locations.race = malloc(n = strlen(locations.path) + 9 + sizeof(storage)); 
 locations.sfv = malloc(n); 
 locations.leader = malloc(n);
 target = malloc(4096);

 d_log("Copying data locations into memory\n");
 raceI.file.name = argv[1] + 5;
 file_set_sfv(locations.sfv, storage "/%s/sfvdata", locations.path);
 file_set_leader(locations.leader, storage "/%s/leader", locations.path);
 file_set_race(locations.race, storage "/%s/racedata", locations.path);

 d_log("Caching release name\n");
 getrelname(locations.path);
 
 d_log("Parsing file extension from filename...\n");
 
 for ( temp_p = name_p = raceI.file.name; *name_p != 0 ; name_p++ ) {
	if ( *name_p == '.' ) {
		temp_p = name_p;
		}
	}

 if ( *temp_p != '.' ) {
	d_log("Got: no extension\n");
	temp_p = name_p;
	} else {
	d_log("Got: %s\n", temp_p);
	temp_p++;
	}
 name_p++;

 d_log("Copying lowercased version of extension to memory\n");
 fileext = malloc(name_p - temp_p);
 memcpy(fileext, temp_p, name_p - temp_p);
 strtolower(fileext); 

 switch ( get_filetype(fileext) ) {
	case 0:
		d_log("File type is: ZIP\n");
		if ((raceI.misc.write_log = matchpath(zip_dirs, locations.path))) {
			raceI.misc.write_log = 1 - matchpath(group_dirs, locations.path);
			} else if (matchpath(sfv_dirs, locations.path)) {
			d_log("Directory matched with sfv_dirs\n");
			break;
			}
		if ( ! fileexists("file_id.diz") ) {
			temp_p = findfileext(".zip");
			if ( temp_p != NULL ) {
				d_log("file_id.diz does not exist, trying to extract it from %s\n", temp_p);
				sprintf(target, "/bin/unzip -qqjnCL %s file_id.diz", temp_p);
				execute(target);
				}
			}

		d_log("Reading diskcount from diz\n");
		raceI.total.files = read_diz("file_id.diz");
		if ( raceI.total.files == 0 ) {
			d_log("Could not get diskcount from diz\n");
			raceI.total.files = 1;
			}
		raceI.total.files_missing = raceI.total.files;

		d_log("Reading race data from file to memory\n");
		readrace(&locations, &raceI, userI, groupI);

		d_log("Caching progress bar\n");
		buffer_progress_bar(&raceI);

		d_log("Removing old complete bar, if any\n");
		removecomplete();
		if ( raceI.total.files_missing < 0 ) {
			raceI.total.files -= raceI.total.files_missing;
			raceI.total.files_missing = 0;
			}
		if ( ! raceI.total.files_missing ) {
			d_log("Creating complete bar\n");
			createstatusbar(convert(&raceI, userI, groupI, zip_completebar) ); 
			} else if ( raceI.total.files_missing < raceI.total.files ) {
			if ( raceI.total.files_missing == 1 ) {
				d_log("Writing INCOMPLETE to %s\n", log);
				writelog(convert(&raceI, userI, groupI, incompletemsg), "INCOMPLETE"); 
				}
			incomplete = 1;
			} else {
			empty_dir = 1;
			}
		break;
	case 1:
		d_log("Reading file count from SFV\n");
		readsfv(&locations, &raceI, 0);

		if ( data_exists(&locations, locations.race) ) {
			d_log("Reading race data from file to memory\n");
			readrace(&locations, &raceI, userI, groupI);
			}

		d_log("Caching progress bar\n");
		buffer_progress_bar(&raceI);
		if (raceI.total.files_missing == raceI.total.files) {
			empty_dir = 1;
			}
		break;
	case 2:
		break;
	case 3:
		d_log("Removing old complete bar, if any\n");
		removecomplete();
		raceI.misc.write_log = matchpath(sfv_dirs, locations.path) > 0 ? 1 - matchpath(group_dirs, locations.path) : 0;

		if ( data_exists(&locations, locations.race) ) {
			d_log("Reading race data from file to memory\n");
			readrace(&locations, &raceI, userI, groupI);
			}

		if ( data_exists(&locations, locations.sfv) ) {
#if ( create_missing_files == TRUE )
			create_missing(raceI.file.name, name_p - raceI.file.name - 1);
#endif
			d_log("Reading file count from SFV\n");
			readsfv(&locations, &raceI, 0);

			d_log("Caching progress bar\n");
			buffer_progress_bar(&raceI);
			}

		if ( raceI.total.files_missing < raceI.total.files ) {
			if ( raceI.total.files_missing == 1 ) {
				d_log("Writing INCOMPLETE to %s\n", log);
				writelog(convert(&raceI, userI, groupI, incompletemsg), "INCOMPLETE");
				}
			incomplete = 1;
			} else {
			d_log("Removing old race data\n");
			remove_data(locations.race);
			if ( findfileext(".sfv") == NULL ) {
				empty_dir = 1;
				} else {
				incomplete = 1;
				}
			}
		break;
	case 4:
		break;
	case 255:
		break;
	}


 if ( empty_dir == 1 ) {
	d_log("Removing all files and directories created by zipscript\n");
	removecomplete();
	if ( data_exists(&locations, locations.sfv)) {
		delete_sfv(&locations);
		}
	unlink(locations.incomplete);
	unlink("file_id.diz"); 
	remove_data(locations.sfv);
	remove_data(locations.race); 
	remove_data(locations.leader); 
	move_progress_bar(1, &raceI);
	}

 if ( incomplete == 1 && raceI.total.files > 0 ) {
	d_log("Creating incomplete indicator\n");
	create_incomplete();
	d_log("Moving progress bar\n");
	move_progress_bar(0, &raceI);
	}

 d_log("Relasing memory\n");
 free(fileext);
 free(target);
 free(raceI.misc.release_name);
 free(locations.path);
 free(locations.race);
 free(locations.sfv);
 free(locations.leader);
 free(raceI.user.name);
 disconnect_mysql();

 d_log("Exit\n");
 return 0;
}
