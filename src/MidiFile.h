/* Parses .mid files and makes them easy to handle (no plans for writing yet)
 * This is can be used on any midi files and provides a way to step through
 * events and separate tracks. This will parse and save all midi events, even
 * if they aren't used for song reading, just so all features are included
 * incase they're ever needed for the future
 */

#ifndef MidiFile_h
#define MidiFile_h

#include <utility>
#include <vector>
#include <stdint.h>
using std::size_t;


class MidiFile
{
public:
   // Constructor will be generic, not initialize anything
   MidiFile();
   
   // Destructor will handle cleaning up tracks and events
   ~MidiFile();
   
   // General types of midi events
   enum MidiEventType
   {
      MidiEventType_Note = 0,        // Basic note event
      MidiEventType_SYSEX = 0xF0,    // System exclusive event
      MidiEventType_SYSEX2 = 0xF7,   // Another header for sysex
      MidiEventType_Meta = 0xFF      // Meta event
   };
   
   // Specific events for midi notes
   enum MidiNoteEvent
   {
      MidiNote_NoteOff = 0x80,            // Note is released
      MidiNote_NoteOn = 0x90,             // Note is pressed
      MidiNote_KeyAfterTouch = 0xA0,      // rest are difficult to explain, see link below
      MidiNote_ControlChange = 0xB0,      // https://www.midi.org/specifications/item/table-1-summary-of-midi-message
      MidiNote_ProgramChange = 0xC0,
      MidiNote_ChannelAfterTouch = 0xD0,
      MidiNote_PitchWheel = 0xE0
   };
   
   // Specific events for meta notes
   enum MIDIMetaEvents
   {
      MidiMeta_SequenceNumber = 0x00,  // Sequence number
      MidiMeta_Text = 0x01,            // Text event
      MidiMeta_Copyright = 0x02,       // Copyright info
      MidiMeta_TrackName = 0x03,       // Track name
      MidiMeta_Instrument = 0x04,      // Instrument number
      MidiMeta_Lyric = 0x05,           // Lyric event
      MidiMeta_Marker = 0x06,          // Marker
      MidiMeta_CuePoint = 0x07,        // Cue point
      MidiMeta_PatchName = 0x08,       // Program (patch) name
      MidiMeta_PortName = 0x09,        // Device (port) name
      MidiMeta_EndOfTrack = 0x2F,      // End of track
      MidiMeta_Tempo = 0x51,           // Tempo event
      MidiMeta_SMPTE = 0x54,           // SMPTE offset (time offset)
      MidiMeta_TimeSignature = 0x58,   // Time signature event
      MidiMeta_KeySignature = 0x59,    // Key signature event
      MidiMeta_Custom = 0x7F,          // Proprietary event
   };
   
   // Midi header chunk, general information of entire file
   struct Header_Chunk
   {
      uint32_t   signature;     // "MThd", signature of a header chunk
      uint32_t   length;        // Length of the file (in bytes)
      uint16_t   format;        // Midi format
      uint16_t   numTracks;     // Number of tracks
      uint16_t   ticksPerBeat;  // Standard ticks/beat
   };
   
   // Midi track chunk, chunks out a track
   struct Track_Chunk
   {
      uint32_t   signature;  // "MTrk", signature of a track chunk
      uint32_t   length;     // Length of the track (in bytes)
   };
   
   // Standard structure for a generic midi event
   struct MidiEvent
   {
      uint32_t tick;      // Tick number this event occurs on
      uint32_t delta;     // Delta ticks between the last event
      uint32_t type;      // The event type
      uint32_t subType;   // The event subtype
      MidiEvent *pNext; // Next event in the sequence
   };
   
   // Midi note event details
   struct MidiEvent_Note : public MidiEvent
   {
      int event;
      int channel;
      int note;
      int velocity;
   };
   
   // Midi sequence number event
   struct MidiEvent_SequenceNumber : public MidiEvent
   {
      int sequence;
   };
   
   // Text event
   struct MidiEvent_Text : public MidiEvent
   {
      char buffer[512];
   };
   
   // Tempo change event
   struct MidiEvent_Tempo : public MidiEvent
   {
      float BPM;
      int microsecondsPerBeat;
   };
   
   // Time offset event
   struct MidiEvent_SMPTE : public MidiEvent
   {
      uint8_t hours, minutes, seconds, frames, subFrames;
   };
   
   // Time signature change event
   struct MidiEvent_TimeSignature : public MidiEvent
   {
      uint8_t numerator, denominator;
      uint8_t clocks;
      uint8_t d;
   };
   
   // Key signature change event
   struct MidiEvent_KeySignature : public MidiEvent
   {
      uint8_t sf;
      uint8_t minor;
   };
   
   // Proprietary event
   struct MidiEvent_Custom : public MidiEvent
   {
      const char *pData;
      uint32_t size;
   };
   
   // Metadata stored in the midi header gets stored in these
   char name[512];      // name of midi file
   int format;          // midi format number
   int ticksPerBeat;    // number of ticks in a beat
   std::vector<MidiEvent*> tracks;  // pointer vector directing to each track
   int numTracks;       // number of tracks in the midi file
};

// Function to read the midi file
MidiFile* ReadMidiFile(const char *pFile, size_t size = 0);

// Overload to open the file first if it was not already done
MidiFile* ReadMidiFile(std::string fileName);

#endif /* MidiFile_h */
