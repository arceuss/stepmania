#include "global.h"
#include "NotesLoaderCHART.h"
#include "RageUtil_CharConversions.h"
#include "RageFile.h"
#include "TimingData.h"
#include "RageUtil.h"
#include "RageLog.h"
#include "BackgroundUtil.h"
#include "NoteData.h"
#include "Song.h"
#include "Steps.h"
#include "GameManager.h"
#include "Difficulty.h"
#include "IniFile.h"
#include "NotesWriterSSC.h"
#include "NotesLoaderSSC.h"
#include <string>
#include <sstream>

/*
 Alright, just a fair warning, this whole thing is complete garbage. I tried making a notesloader the legit way, but it never
 worked, so instead it rewrites it as an SSC file then calls the SSC loader
 This isn't a loader at all, this is just a rewriter because I don't know how this is supposed to work.
 
 Coming back to this after a long break and lots of research. It turns out this stupid thing was parsing charts with roughly
 the same accuracy as the original FoF, which was rather impressive. I'm trying to get it to be more accurate than FoFiX, at
 which point it's really up to the custom chart markers/midi translator programs to make a faithfully accurate chart. The
 limitation there is the midi translators don't care for certain parts, such as forced notes, as well as features added after
 GHIII, like slider notes and tap notes during holds. For that level accuracy, I'll need to write a MIDILoader next, then
 songs can be directly ripped from any Guitar Hero or Rock Band game and be more accurate than PhaseShift.
 */

NoteData parseNoteSection(std::istringstream iss) {
   NoteData newNotes;
   std::string line;
   
   while( std::getline(iss,line) )
   {
      // Remove tabs
      line.erase(std::remove(line.begin(), line.end(), '\t'), line.end());
      std::vector<std::string> vsWords;
      std::istringstream lineIss(line);
      // Separate by spaces
      copy( std::istream_iterator<std::string>(lineIss), std::istream_iterator<std::string>(), back_inserter(vsWords) );
   }
   
   return newNotes;
}
// TODO: something's still wrong, time for refactoring later

void parseHeader(std::istringstream iss) {
   
}

/* TODO: this needs lots of refactoring and extracting of methods and functionality.
 * If I ever invent a time machine, I'm going to slap my past self for some of this code
 */
void ReadBuf( const char *buf, int len, Song &outSong, Steps &outSteps, bool parseSongInfo, std::string sFilePath )
{
   bool bDontReadThis = false;
   int iMode = 0;
   
   // vectors so columns can be checked individually
   std::vector<int> iPrevNoteMark(5);
   int iPrevNoteTrack = -1;
   std::vector<int> iPrevNoteLength(5);
   std::vector<bool> bPrevNoteHOPO(5);
   for(int i=0; i<5; i++) {
      iPrevNoteMark[i] = -1;
      iPrevNoteLength[i] = -1;
      bPrevNoteHOPO[i] = false;
   }
   
   // For keeping track of forced note markers, denoted by "E *" or "N 5"
   int iLastForcedRow = -1;
   // For keeping track of forced tap notes, denoted by "E T"
   int iLastTapRow = -1;
   // HOPOs should never immediately follow chords because reasons
   int iLastChordRow = -1;
   
   // 192 is default resolution per beat in some (poorly done) charts, in GH and RB, it's actually 480
   int resolution = 192;
   bool readingNotes = false;
   
   // HOPO frequency varies in FoFiX because HOPO reading sucks in Guitar Hero
   int iHopoResolution = 120;
   
   // special vector for storing special things
   std::vector<std::string> headerInfo(3);
   
   // NoteData to store stuff that's being parsed
   NoteData *notedata = new NoteData();
   // TimingData to store other stuff being parsed
   TimingData timing;
   // Pointer to Steps that may or may not need to be added, depending on if we're parsing for a Song or not
   Steps* pNewNotes = NULL;
   
   // Set notedata tracks
   notedata->SetNumTracks(6);
   
   // Screw efficiency, just gonna parse this word by word like I was gonna do with Python
   std::string bufStr(buf);
   std::istringstream iss(bufStr);
   std::string line;
   // Parse line-by-line
   while( std::getline(iss,line) )
   {
      // Remove tabs
      line.erase(std::remove(line.begin(), line.end(), '\t'), line.end());
      std::vector<std::string> vsWords;
      std::istringstream lineIss(line);
      // Separate by spaces
      copy( std::istream_iterator<std::string>(lineIss), std::istream_iterator<std::string>(), back_inserter(vsWords) );
      int i = 0;
      int numWords = vsWords.size();
      // Don't need to parse through every word, since each of these if blocks parse the entire line
      
      /**
       Change the mode once you hit a section tag. Until the actual notes, the tags should always be in the order:
       Song, SyncTrack, Events, then comes the notes which are tagged by difficulty and game mode
       */
      if( vsWords[i].at(0) == '[' ) {
         if( iMode < 4 ) ++iMode;

         bDontReadThis = false;
         if( iMode == 4 ) {
            
            // If it's not single mode, we don't care about it, for now
            if( vsWords[i].find("Single") != std::string::npos ) {
               
               // reset these between songs
               iPrevNoteTrack = -1;
               for(int j=0; j<5; j++) {
                  iPrevNoteMark[j] = -1;
                  iPrevNoteLength[j] = -1;
                  
               }
               
               // If we're parsing a song, add the previous notes parsed, then get space for new steps
               if( parseSongInfo ) {
                  if( readingNotes ) {
                     pNewNotes->m_Timing = timing;
                     pNewNotes->SetNoteData( *notedata );
                     pNewNotes->TidyUpData();
                     outSong.AddSteps( pNewNotes );
                     
                     // Reset notedata for new set of steps
                     notedata = new NoteData();
                     notedata->SetNumTracks(6);
                  } else {
                     outSong.m_SongTiming = timing;
                  }
                  pNewNotes = outSong.CreateSteps();
                  pNewNotes->SetChartStyle("Guitar");
                  pNewNotes->SetCredit( headerInfo[1] );
                  pNewNotes->SetDescription( headerInfo[1] );
                  pNewNotes->SetMusicFile( headerInfo[2] );
                  // Charts only load for guitar mode... for now.
                  pNewNotes->m_StepsType = StepsType_guitar_solo;
               }
               
               if( !readingNotes ) readingNotes = true;
               if( vsWords[i].find("Expert") != std::string::npos ) {
                  // Expert == Challenge
                  if( !parseSongInfo && outSteps.GetDifficulty() != Difficulty_Challenge ) {
                     // This is the incorrect difficulty for the desired steps, continue to the next noteinfo tag
                     bDontReadThis = true;
                     continue;
                  }
                  if( parseSongInfo ) {
                     pNewNotes->SetDifficulty( Difficulty_Challenge );
                     pNewNotes->SetChartName( headerInfo[0]+" - Expert" );
                     continue;
                  }
                  
               } else if( vsWords[i].find("Hard") != std::string::npos ) {
                  if( !parseSongInfo && outSteps.GetDifficulty() != Difficulty_Hard ) {
                     bDontReadThis = true;
                     continue;
                  }
                  if( parseSongInfo ) {
                     pNewNotes->SetDifficulty( Difficulty_Hard );
                     pNewNotes->SetChartName( headerInfo[0]+" - Hard" );
                     continue;
                  }
                  
               } else if( vsWords[i].find("Medium") != std::string::npos ) {
                  if( !parseSongInfo && outSteps.GetDifficulty() != Difficulty_Medium ) {
                     bDontReadThis = true;
                     continue;
                  }
                  if( parseSongInfo ) {
                     pNewNotes->SetDifficulty( Difficulty_Medium );
                     pNewNotes->SetChartName( headerInfo[0]+" - Medium" );
                     continue;
                  }
                  
               } else if( vsWords[i].find("Easy") != std::string::npos ) {
                  if( !parseSongInfo && outSteps.GetDifficulty() != Difficulty_Easy ) {
                     bDontReadThis = true;
                     continue;
                  }
                  if( parseSongInfo ) {
                     pNewNotes->SetDifficulty( Difficulty_Easy );
                     pNewNotes->SetChartName( headerInfo[0]+" - Easy" );
                     continue;
                  }
               }
               
               // Custom Guitar Hero charts have no beginner difficulty, this was added starting in GH World Tour
            } else {
               bDontReadThis = true;
               continue;
            }
         } else {
            continue;
         }
      }
      
      /* Curly braces are just filler */
      if( !bDontReadThis && vsWords[i].at(0) != '{' && vsWords[i].at(0) != '}' )
      {
         if( iMode == 1 ) {
            // Parse song information
            // Separation by words also means that titles, artists and other info is likely broken up, stitch it back together
            
            int j = 2;
            std::string wholeString = "";
            while(j < numWords) {
               if(j>2) wholeString += " ";
               wholeString += vsWords[j];
               j++;
            }
            
            if( !vsWords[i].compare("Name") )
            {
               std::string mainTitle = wholeString.substr(1, wholeString.size()-2);
               headerInfo[0] = mainTitle;
               if( parseSongInfo ) outSong.m_sMainTitle = mainTitle;
            } else if( !vsWords[i].compare("Artist") )
            {
               if( parseSongInfo ) outSong.m_sArtist = wholeString.substr(1, wholeString.size()-2);
            } else if( !vsWords[i].compare("Charter") )
            {
               headerInfo[1] = wholeString.substr(1, wholeString.size()-2);
            } else if( !vsWords[i].compare("Offset") )
            {
               timing = TimingData(-1 * atof(vsWords[i+2].c_str()));
               
               // Dunno if this will actually do anything, but it seems to be defaults put into SSC files for some reason?
               timing.AddSegment(ComboSegment(0.0,1,1));
               timing.AddSegment(ScrollSegment(0.0,1.0));
            } else if( !vsWords[i].compare("Resolution") )
            {
               /* Here's the thing, bad charts have a default resolution of 192 ticks per beat. Comparatively, Stepmania uses
                * 192 ticks per MEASURE as a maximum (real Guitar Hero and Rock Band charts have a resolution of 480 per beat)
                * This causes a lot of problems. If the resolution is >48, just make it 48
                * But only for setting the tickcount segment, still need the original resolution for note and timing parsing
                * Also, parse the ini here since it's only useful for HOPO resolutions
                */
               resolution = atoi(vsWords[i+2].c_str());
               
               IniFile ini;
               if( !ini.ReadFile( sFilePath + "song.ini" ) )
               {
                  // could not find ini file, oh well
                  iHopoResolution = resolution / 4;
               } else
               {
                  bool eightNoteHopo = false;
                  int hopoRes = 2;
                  
                  // first, check if there's any special HOPO frequency because FoFiX cheats
                  if( ini.GetValue("song", "hopofreq", hopoRes) ) {
                     switch( hopoRes ){
                           // example hopoResolutions given if resolution is 480
                        case 0:
                           // fewest HOPOs
                           iHopoResolution = resolution / 2; // ~= 240
                           break;
                        case 1:
                           // few HOPOs
                           iHopoResolution = resolution * (3 / 8);// ~= 180
                           break;
                        case 2:
                        default:
                           // normal HOPOs
                           iHopoResolution = resolution / 4; // ~= 120 standard
                           break;
                        case 3:
                           // more HOPOs
                           iHopoResolution = resolution * (3 / 16);// ~= 90
                           break;
                        case 4:
                           // most HOPOs
                           iHopoResolution = resolution / 8; // ~= 60
                           break;
                        // song.ini details say this can go up to 5, but FoFiX source code says otherwise
                     }
                  }
                  // next, if eighthnotes count as hopos, divide the hopoResolution in half
                  if( ini.GetValue("song", "eighthnote_hopo", eightNoteHopo) ) {
                     if( eightNoteHopo ) iHopoResolution /= 2;
                  }
               }
               
               if( resolution <= 48 ) timing.AddSegment(TickcountSegment(0, resolution));
               else timing.AddSegment(TickcountSegment(0, 48));
            } else if( !vsWords[i].compare("Difficulty") )
            {
               /* Uh, skip this for now, causes Stepmania to crash...
                if( parseSongInfo ) pNewNotes->SetMeter(atoi(vsWords[i+2].c_str()) + 1);
                else outSteps->SetMeter(atoi(vsWords[i+2].c_str()) + 1);
                */
            } else if( !vsWords[i].compare("PreviewStart") )
            {
               if( parseSongInfo ) outSong.m_fMusicSampleStartSeconds = atof(vsWords[i+2].c_str());
            } else if( !vsWords[i].compare("PreviewEnd") )
            {
               if( parseSongInfo )
               {
                  // Default to 12 second length, just seems like the default in StepMania
                  float estSampleSec = atof(vsWords[i+2].c_str()) - outSong.m_fMusicSampleStartSeconds;
                  if( estSampleSec <= 12 ) outSong.m_fMusicSampleStartSeconds = 12;
                  else outSong.m_fMusicSampleLengthSeconds = estSampleSec;
               }
            } else if( !vsWords[i].compare("MusicStream") )
            {
               // TODO: also parse GuitarStream and RhythmStream to have multiple audio tracks...
               // somehow... if Stepmania even allows it?
               std::string songFile = wholeString.substr(1, wholeString.size()-2);
               if( parseSongInfo ) outSong.m_sMusicFile = songFile;
               else outSteps.SetMusicFile( songFile );
               headerInfo[2] = songFile;
            }
         } else if( iMode == 2 ) {
            // Parse BPM and time signature changes
            int startMark = atoi(vsWords[i].c_str());
            float newBPM = atof(vsWords[i+3].c_str()) / 1000;
            
            if( !vsWords[i+2].compare( "B" ) )
            {
               if( startMark == 0 ) timing.AddSegment( BPMSegment(0, newBPM ));
               else timing.SetBPMAtBeat((float)startMark/resolution, newBPM);
            } else if( !vsWords[i+2].compare( "TS" ) )
            {
               // Fun fact, Guitar Hero songs are always in a time signature of x/4
               timing.AddSegment( TimeSignatureSegment(BeatToNoteRow(atof(vsWords[i].c_str()) / resolution), atoi(vsWords[i+3].c_str()), 4) );
            }
         } else if( iMode == 3 ) {
            // Parse section labels
            // on good charts, this will only be the section title
            // on bad charts, this will include a bunch of garbage too
            int end = vsWords.size()-1;
            std::string sectionTitle = vsWords[end].substr(0, vsWords[end].size()-1);
            // some charts have erroneous sections and that makes this annoying
            if(sectionTitle.size() > 2) {
               timing.AddSegment(LabelSegment(BeatToNoteRow(atof(vsWords[i].c_str())/resolution),sectionTitle));
            }
         } else if( iMode == 4 ) {
            
            // Parse the notedata
            // Parsing special cases first
            if( !vsWords[i+2].compare( "E" ) )
            {
               // This denotes a forced note, this is usually because a charter forgot to change this to "N 5"
               if( !vsWords[i+3].compare( "*" ) )
               {
                  iLastForcedRow = atoi(vsWords[i].c_str());
                  
                  // Search back and change any notes on this row to their opposite
                  TapNote tn = TAP_EMPTY;
                  
                  for( int l=0; l<5; l++ ) {
                     tn = notedata->GetTapNote(l, BeatToNoteRow((float)iLastForcedRow/resolution));
                     
                     if( tn.type == TapNoteType_Gem || tn.type == TapNoteType_GemHold )
                     {
                        if( tn.iDuration )
                        {
                           notedata->AddHoldNote(l, BeatToNoteRow((float)iLastForcedRow/resolution),
                                    BeatToNoteRow((float)iLastForcedRow/resolution)+tn.iDuration, TAP_ORIGINAL_HOPO_HOLD);
                        } else
                        {
                           notedata->SetTapNote(l, BeatToNoteRow((float)iLastForcedRow/resolution), TAP_ORIGINAL_HOPO);
                        }
                     } else if( tn.type == TapNoteType_HOPO || tn.type == TapNoteType_HOPOHold )
                     {
                        if( tn.iDuration )
                        {
                           notedata->AddHoldNote(l, BeatToNoteRow((float)iLastForcedRow/resolution),
                                       BeatToNoteRow((float)iLastForcedRow/resolution)+tn.iDuration, TAP_ORIGINAL_GEM_HOLD);
                        } else
                        {
                           notedata->SetTapNote(l, BeatToNoteRow((float)iLastForcedRow/resolution), TAP_ORIGINAL_GEM);
                        }
                     }
                  }
               }
               
               // This denotes a tap note, or slider note, it's literally a tap note in Stepmania
               if( !vsWords[i+3].compare( "T" ) )
               {
                  iLastTapRow = atoi(vsWords[i].c_str());
                  
                  // Search back and change any notes on this row to tap notes
                  TapNote tn = TAP_EMPTY;
                  
                  for( int l=0; l<5; l++ ) {
                     tn = notedata->GetTapNote(l, BeatToNoteRow((float)iLastTapRow/resolution));
                     
                     if( tn != TAP_EMPTY ) {
                        if( tn.iDuration )
                        {
                           notedata->AddHoldNote(l, BeatToNoteRow((float)iLastTapRow/resolution),
                                       BeatToNoteRow((float)iLastForcedRow/resolution)+tn.iDuration, TAP_ORIGINAL_HOLD_HEAD);
                        } else
                        {
                           notedata->SetTapNote(l, BeatToNoteRow((float)iLastTapRow/resolution), TAP_ORIGINAL_TAP);
                        }
                     }
                  }
               }
            }
            
            if( !vsWords[i+2].compare( "N" ) )
            {
               int iNoteTrack = atoi(vsWords[i+3].c_str());
               
               /* Track 5 is used to denote forced notes (sometimes E * is), this means that any note at the same
                * time as that are toggled between HOPO and strum notes.
                */
               if( iNoteTrack == 5 )
               {
                  // literally just copy and paste what's above with a continue at the end
                  iLastForcedRow = atoi(vsWords[i].c_str());
                  
                  // Search back and change any notes on this row to their opposite
                  TapNote tn = TAP_EMPTY;
                  
                  for( int l=0; l<5; l++ ) {
                     tn = notedata->GetTapNote(l, BeatToNoteRow((float)iLastForcedRow/resolution));
                     
                     if( tn.type == TapNoteType_Gem || tn.type == TapNoteType_GemHold )
                     {
                        if( tn.iDuration )
                        {
                           notedata->AddHoldNote(l, BeatToNoteRow((float)iLastForcedRow/resolution),
                                       BeatToNoteRow((float)iLastForcedRow/resolution)+tn.iDuration, TAP_ORIGINAL_HOPO_HOLD);
                        } else
                        {
                           notedata->SetTapNote(l, BeatToNoteRow((float)iLastForcedRow/resolution), TAP_ORIGINAL_HOPO);
                        }
                     } else if( tn.type == TapNoteType_HOPO || tn.type == TapNoteType_HOPOHold )
                     {
                        if( tn.iDuration )
                        {
                           notedata->AddHoldNote(l, BeatToNoteRow((float)iLastForcedRow/resolution),
                                       BeatToNoteRow((float)iLastForcedRow/resolution)+tn.iDuration, TAP_ORIGINAL_GEM_HOLD);
                        } else
                        {
                           notedata->SetTapNote(l, BeatToNoteRow((float)iLastForcedRow/resolution), TAP_ORIGINAL_GEM);
                        }
                     }
                  }
                  
                  continue;
               }
               
               /* A note on sustained notes, Guitar Hero likes to have holds end exactly on beats, unfortunately this means that
                * sometimes a hold note can overlap into the next hold if they're in the same track. Need to check for this and
                * correct it by shortening the first hold slightly (32nd note shorter than full beat)
                * Also, rounding errors in Chart2Mid2Chart mean it can be off by 1
                */
               int iNoteMark = atoi(vsWords[i].c_str());
               int iNoteLength = atoi(vsWords[i+4].c_str());
               
               if( iPrevNoteLength[iNoteTrack] + iPrevNoteMark[iNoteTrack] + 1 >= iNoteMark ) {
                  // sustain note correction
                  notedata->SetTapNote(iNoteTrack, BeatToNoteRow((float)iPrevNoteMark[iNoteTrack]/resolution), TAP_EMPTY);
                  iPrevNoteLength[iNoteTrack] = iNoteMark - iPrevNoteMark[iNoteTrack] - (resolution / 8);
                  
                  if( !bPrevNoteHOPO[iNoteTrack] ) {
                     if( iPrevNoteLength[iNoteTrack] > 0 )
                        notedata->AddHoldNote(iNoteTrack, BeatToNoteRow((float)iPrevNoteMark[iNoteTrack]/resolution),
                                             BeatToNoteRow((float)(iPrevNoteMark[iNoteTrack]+iPrevNoteLength[iNoteTrack])/resolution), TAP_ORIGINAL_GEM_HOLD);
                     else
                        notedata->SetTapNote(iNoteTrack, BeatToNoteRow((float)iPrevNoteMark[iNoteTrack]/resolution), TAP_ORIGINAL_GEM);
                  } else {
                     if( iPrevNoteLength[iNoteTrack] > 0 )
                        notedata->AddHoldNote(iNoteTrack, BeatToNoteRow((float)iPrevNoteMark[iNoteTrack]/resolution),
                                             BeatToNoteRow((float)(iPrevNoteMark[iNoteTrack]+iPrevNoteLength[iNoteTrack])/resolution), TAP_ORIGINAL_HOPO_HOLD);
                     else
                        notedata->SetTapNote(iNoteTrack, BeatToNoteRow((float)iPrevNoteMark[iNoteTrack]/resolution), TAP_ORIGINAL_HOPO);
                  }
               }
               
               // If this note and the previous note are on the same beat and the previous note was a HOPO,
               // Change it to a normal gem
               for( int k=0; k<5; ++k )
               {
                  if( bPrevNoteHOPO[k] && k != iNoteTrack && std::abs(iNoteMark - iPrevNoteMark[k]) <= 1 )
                  {
                     iLastChordRow = iNoteMark;
                     
                     if( iPrevNoteLength[k] ) // Previous note was a hold
                     {
                        notedata->AddHoldNote(iPrevNoteTrack, BeatToNoteRow((float)iPrevNoteMark[k]/resolution),
                                             BeatToNoteRow((float)(iPrevNoteMark[k]+iPrevNoteLength[k])/resolution), TAP_ORIGINAL_GEM_HOLD);
                     } else
                     {
                        notedata->SetTapNote(iPrevNoteTrack, BeatToNoteRow((float)iPrevNoteMark[k]/resolution), TAP_ORIGINAL_GEM);
                     }

                     bPrevNoteHOPO[k] = false;
                  }
               }
               
               // Specially marked tap notes override all HOPO rules
               if( std::abs(iLastTapRow - iNoteMark) <= 1 && iLastTapRow != -1 )
               {
                  if( iNoteLength )
                  {
                     notedata->AddHoldNote(iNoteTrack, BeatToNoteRow((float)iNoteMark/resolution),
                                          BeatToNoteRow((float)(iNoteMark+iNoteLength)/resolution), TAP_ORIGINAL_HOLD_HEAD);
                  } else
                  {
                     notedata->SetTapNote(iNoteTrack, BeatToNoteRow((float)iNoteMark/resolution), TAP_ORIGINAL_TAP);
                  }
                  bPrevNoteHOPO[iNoteTrack] = false;
               }
               else
               {
               
                  /* If the difference between this note and the last is <= a 16th note (and > 0), and they're
                   * on different tracks, AND this isn't the first note, then make this a HOPO
                   * If the row is forced, strum notes become HOPOs, and HOPOs become strum notes
                   * And as an extra rule, if this note is following a chord and the note was part of the chord,
                   * then it cannot be a HOPO
                   */
                  bool ShouldBeHOPO = false;
                  
                  for( int k=0; k<5; ++k )
                  {
                     if((std::abs(iNoteMark - iPrevNoteMark[k]) - 1 <= iHopoResolution) && (iNoteTrack != iPrevNoteTrack) &&
                        (iPrevNoteMark[k] != -1)) ShouldBeHOPO = true;
                     if( (std::abs(iNoteMark - iPrevNoteMark[k]) <= 1) || (std::abs(iLastChordRow - iNoteMark) <= 1 &&
                                                                           iLastChordRow != -1) )
                     {
                        ShouldBeHOPO = false;
                        break;
                     }
                     if( iPrevNoteTrack != -1 && iPrevNoteMark[iNoteTrack] != -1 && iPrevNoteMark[iPrevNoteTrack] != -1 &&
                        std::abs(iPrevNoteMark[iNoteTrack] - iPrevNoteMark[iPrevNoteTrack]) <= 1 &&
                        iPrevNoteTrack != iNoteTrack)
                     {
                        ShouldBeHOPO = false;
                        break;
                     }
                  }
                  
                  // Reverse the note if the row was marked to be forced
                  if( std::abs(iLastForcedRow - iNoteMark) <= 1 && iLastForcedRow != -1 ) ShouldBeHOPO = !ShouldBeHOPO;
                  
                  if(ShouldBeHOPO)
                  {
                     if( iNoteLength )
                     {
                        notedata->AddHoldNote(iNoteTrack, BeatToNoteRow((float)iNoteMark/resolution),
                                             BeatToNoteRow((float)(iNoteMark+iNoteLength)/resolution), TAP_ORIGINAL_HOPO_HOLD);
                     } else
                     {
                        notedata->SetTapNote(iNoteTrack, BeatToNoteRow((float)iNoteMark/resolution), TAP_ORIGINAL_HOPO);
                     }
                     bPrevNoteHOPO[iNoteTrack] = true;
                  }
                  
                  // Otherwise, plop a Gem in there
                  else
                  {
                     if( iNoteLength )
                     {
                        notedata->AddHoldNote(iNoteTrack, BeatToNoteRow((float)iNoteMark/resolution),
                                             BeatToNoteRow((float)(iNoteMark+iNoteLength)/resolution), TAP_ORIGINAL_GEM_HOLD);
                     } else
                     {
                        notedata->SetTapNote(iNoteTrack, BeatToNoteRow((float)iNoteMark/resolution),TAP_ORIGINAL_GEM);
                     }
                     bPrevNoteHOPO[iNoteTrack] = false;
                  }
               }
               
               iPrevNoteMark[iNoteTrack] = iNoteMark;
               iPrevNoteTrack = iNoteTrack;
               iPrevNoteLength[iNoteTrack] = iNoteLength;
            }
         }
      }
   }
   
   // If we reached the end of the file while still reading notes, add them to the song, if we're parsing that
   if( parseSongInfo && readingNotes ) {
      pNewNotes->m_Timing = timing;
      pNewNotes->SetNoteData( *notedata );
      pNewNotes->TidyUpData();
      outSong.AddSteps(pNewNotes);
   }
   if( !parseSongInfo )
   {
      outSteps.m_Timing = timing;
      outSteps.SetNoteData( *notedata );
      outSteps.TidyUpData();
   }
}

// Returns true if successful, false otherwise
bool ReadFile( std::string sNewPath, Song &outSong, Steps &outSteps, bool parseSongInfo )
{
   RageFile f;
   /* Open a file. */
   if( !f.Open( sNewPath ) )return false;
   
   // path to the base dir
   std::string sBasePath = sNewPath.substr(0, sNewPath.find_last_of("/")+1);
   
   // allocate a string to hold the file
   std::string FileString;
   FileString.reserve( f.GetFileSize() );
   
   int iBytesRead = f.Read( FileString );
   if( iBytesRead == -1 )return false;
   
   Song tempSong;
   Steps tempSteps = NULL;
   if( parseSongInfo ) tempSong = Song();
   else tempSteps = Steps(outSteps);
   
   std::string sscFile = sNewPath.substr(0,sNewPath.length()-5) + "ssc";
   std::string dir = sNewPath.substr(0,sNewPath.find_last_of("/\\")+1);
   
   ReadBuf( FileString.c_str(), iBytesRead, tempSong, tempSteps, parseSongInfo, sBasePath );
   
   // TODO: can the above operation fail somehow? Return false if it does
   return true;
   
   /* This totally works, but it doesn't work if I just call ReadBuf with the output files */
   /*
   NotesWriterSSC::Write(sscFile, tempSong, tempSong.GetAllSteps(), false);
   
   SSCLoader loaderSSC;
   
   if( parseSongInfo ) {
      return loaderSSC.LoadFromDir(dir,outSong);
   } else {
      return loaderSSC.LoadNoteDataFromSimfile(sNewPath, outSteps);
   }
    */
}

void CHARTLoader::GetApplicableFiles( const std::string &sPath, std::vector<std::string> &out )
{
   GetDirListing( sPath + std::string("*.chart"), out );
}

bool CHARTLoader::LoadFromDir( const std::string &sDir, Song &out ) {
   LOG->Trace( "CHARTLoader::LoadFromDir(%s)", sDir.c_str() );
   
   std::vector<std::string> arrayCHARTFileNames;
   GetDirListing( sDir + std::string("*.chart"), arrayCHARTFileNames );
   
   // We shouldn't have been called to begin with if there were no CHARTs.
   ASSERT( arrayCHARTFileNames.size() != 0 );
   
   std::string dir = out.GetSongDir();
   
   Steps *tempSteps = NULL;
   
   // Only need to use the first file, since there should only be 1
   return ReadFile( dir+arrayCHARTFileNames[0], out, *tempSteps, true );
}

bool CHARTLoader::LoadNoteDataFromSimfile( const std::string & cachePath, Steps &out ) {
   Song *tempSong = NULL;
   // This is simple since the path is already given to us
   return ReadFile(cachePath, *tempSong, out, false);
}
