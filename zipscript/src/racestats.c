#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "../conf/zsconfig.h"
#include "../include/objects.h"
#include "../include/macros.h"
#include "../../config.h"

static struct USERINFO  **userI;
static struct GROUPINFO **groupI;
static struct VARS      raceI;
static struct LOCATIONS locations;

extern void updatestats(struct VARS *raceI, struct USERINFO **userI, struct GROUPINFO **groupI,
                        char *usern, char *group, long filesize, long speed, long startsec, long startusec);
extern void sortstats(struct VARS *raceI, struct USERINFO **userI, struct GROUPINFO **groupI);

extern char* convert(struct VARS *raceI, struct USERINFO **userI, struct GROUPINFO **groupI, char *instr);
extern char* readsfv_file(struct LOCATIONS *locations, struct VARS *raceI, int getfcount);

extern void readrace_file(struct LOCATIONS *locations, struct VARS *raceI, struct USERINFO **userI, struct GROUPINFO **groupI);
extern char* readsfv_mysql(struct LOCATIONS *locations, struct VARS *raceI, int getfcount);

extern void readrace_mysql(struct LOCATIONS *locations, struct VARS *raceI, struct USERINFO **userI, struct GROUPINFO **groupI);
extern void read_write_leader_mysql(struct LOCATIONS *locations, struct VARS *raceI, struct USERINFO **userI);
extern short table_exists(struct LOCATIONS *locations, char *table);

#include "zsfunctions.h"

#ifdef HAVE_MYSQL   
 #define data_exists(paths, datalocation) table_exists(paths, datalocation)
 #define sql_set_race   sprintf 
 #define sql_set_sfv    sprintf
 #define file_set_race
 #define file_set_sfv
 #define maketempdir

 #define sql_get_index(x)	index = get_index_mysql(x)

 #define readsfv        readsfv_mysql
 #define readrace       readrace_mysql
#else
 #define data_exists(paths, datalocation) fileexists(datalocation)
 #define sql_set_sfv
 #define sql_set_race
 #define sql_set_leader
 #define file_set_race   sprintf
 #define file_set_sfv    sprintf

 #define sql_get_index(x)

 #define readsfv        readsfv_file
 #define readrace       readrace_file
 #define connect_mysql()
 #define disconnect_mysql()
#endif


char      error_msg  [80],
          output     [2048],   
          output2    [1024];
short     varelease  = 2,       // Various Artists release
          ERROR_CODE = 0;



void getrelname(char *directory) {
 int cnt,
     l,
     n = 0,
     k = 2;
 long index;
 char *directoryarray[2];
   
 for ( cnt = strlen(directory) - 1 ; k && cnt ; cnt-- )
  if ( directory[cnt] == '/' ) {
   k--;
   directoryarray[k] = malloc(n + 1);
   strncpy(directoryarray[k], directory + cnt + 1, n);
   directoryarray[k][n] = 0;
   n = 0;
  } else n++;
  
 l = strlen(directoryarray[1]);

 sql_get_index(&locations);
 sql_set_race(locations.race, "R_%i", index);
 sql_set_sfv(locations.sfv, "S_%i", index);
   
 if (( ! strncasecmp(directoryarray[1], "CD"  , 2) && l <= 4 ) ||
     ( ! strncasecmp(directoryarray[1], "DISC", 4) && l <= 6 )) {
  n = strlen(directoryarray[0]);
  raceI.misc.release_name    = malloc(n + 18);
  sprintf(raceI.misc.release_name, "%s/\\002%s\\002", directoryarray[0], directoryarray[1]); 
 } else {
  raceI.misc.release_name    = malloc(l + 12);
  sprintf(raceI.misc.release_name, "\\002%s\\002", directoryarray[1]);
 }
 free(directoryarray[0]);
 free(directoryarray[1]);
}




/* CORE CODE - NOT MUCH HERE - JUST CALLS FUNCTIONS IN RIGHT ORDER & SET FEW VARIABLES */
int main( int argc, char **argv ) {
   int n;

   if ( argc == 1 ) {
    printf("Usage: %s <path>\n", argv[0]);
    exit(0);
   }
   connect_mysql();
  
   locations.path = malloc( n = strlen(argv[1]) + strlen(site_root) + 1 ); 
   locations.race = malloc( n += 10 + strlen(storage) );
   locations.sfv  = malloc( n + 10 );

   userI    = malloc( sizeof(int) * 30 );
   groupI   = malloc( sizeof(int) * 30 );

   raceI.misc.slowest_user[0] = 30000;
   raceI.misc.fastest_user[0] =
   raceI.total.speed =
   raceI.total.files_missing =
   raceI.total.files =
   raceI.total.size =
   raceI.total.users =
   raceI.total.groups = 0;
   raceI.user.name = "";
   raceI.file.name = "";


   strcpy(locations.path, site_root);
   strcat(locations.path, argv[1]);

   n = strlen(locations.path);
   if ( locations.path[n] == '/' ) {
	locations.path[n] = 0;
	}
   

   if ( chdir(locations.path) ) goto END;
   #ifdef HAVE_MYSQL
    strcpy(locations.path, argv[1]);
   #endif

   getrelname(locations.path);

   file_set_race(locations.race, site_root storage "/%s/racedata", argv[1]);
   if ( ! data_exists(&locations, locations.race) ) goto END;

   readrace(&locations, &raceI, userI, groupI);
   file_set_sfv(locations.sfv, site_root storage "/%s/sfvdata", argv[1]);

   if ( ! data_exists(&locations, locations.sfv) ) {
    if ( data_exists(&locations, locations.sfv) ) {
     raceI.total.files = read_diz("file_id.diz");
     raceI.total.files_missing += raceI.total.files;
    } else {
     raceI.total.files -= raceI.total.files_missing;
     raceI.total.files_missing = 0;
    }
   } else {
    readsfv(&locations, &raceI, 0 );
   }

   sortstats(&raceI, userI, groupI);
   if ( ! raceI.total.users ) goto END;

   printf("%s\n", convert(&raceI, userI, groupI, stats_line));

END:
   disconnect_mysql();
   free(locations.path);
   free(locations.race);
   free(raceI.misc.release_name);
   free(locations.sfv);

   exit(0);
}

