/** @brief NotesLoaderMID - loads notes from .mid files, good 'ol fashioned midi */

#ifndef NotesLoaderMID_H
#define NotesLoaderMID_H

class Song;
class Steps;

namespace MIDILoader
{
   void GetApplicableFiles( const std::string &sPath, std::vector<std::string> &out );
   bool LoadFromDir( const std::string &sDir, Song &out );
   bool LoadNoteDataFromSimfile( const std::string & cachePath, Steps &out );
}

#endif /* NotesLoaderMID_H */
