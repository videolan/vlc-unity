#include <stdio.h>
#include <stdlib.h>
#include <vlc/vlc.h>

void playFilm()
{
  libvlc_instance_t * inst;
  libvlc_media_player_t *mp;
  libvlc_media_t *m;
     
  /* Load the VLC engine */
  inst = libvlc_new (0, NULL);
  
  /* Create a new item */
  m = libvlc_media_new_location (inst, "file:///media/noMansLand/Movies/Antman (2015) [1080p].mkv");
  //m = libvlc_media_new_path (inst, "/path/to/test.mov");
        
  /* Create a media player playing environement */
  mp = libvlc_media_player_new_from_media (m);
     
  /* No need to keep the media now */
  libvlc_media_release (m);
  
  /* play the media_player */
  libvlc_media_player_play (mp);
    
  sleep (10); /* Let it play a bit */

  /* play the media_player */
  libvlc_media_player_pause (mp);

  sleep (5); /* Let it pause a bit */

  /* play the media_player */
  libvlc_media_player_play (mp);

  sleep (10); /* Let it play a bit more */
  
  /* Stop playing */
  libvlc_media_player_stop (mp);
 
  /* Free the media_player */
  libvlc_media_player_release (mp);
 
  libvlc_release (inst);
 
  return 0;
}
