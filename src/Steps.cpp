/* This stores a single note pattern for a song.
 *
 * We can have too much data to keep everything decompressed as NoteData, so most
 * songs are kept in memory compressed as SMData until requested.  NoteData is normally
 * not requested casually during gameplay; we can move through screens, the music
 * wheel, etc. without touching any NoteData.
 *
 * To save more memory, if data is cached on disk, read it from disk on demand.  Not
 * all Steps will have an associated file for this purpose.  (Profile edits don't do
 * this yet.)
 *
 * Data can be on disk (always compressed), compressed in memory, and uncompressed in
 * memory. */
#include "global.h"
#include "Steps.h"
#include "StepsUtil.h"
#include "GameState.h"
#include "Song.h"
#include "RageUtil.h"
#include "RageLog.h"
#include "NoteData.h"
#include "GameManager.h"
#include "SongManager.h"
#include "NoteDataUtil.h"
#include "NotesLoaderSSC.h"
#include "NotesLoaderSM.h"
#include "NotesLoaderSMA.h"
#include "NotesLoaderDWI.h"
#include "NotesLoaderKSF.h"
#include "NotesLoaderBMS.h"
#include "NotesLoaderChart.h"
#include "NotesLoaderMID.h"
#include <algorithm>

/* register DisplayBPM with StringConversion */
#include "EnumHelper.h"

using std::vector;

static const char *DisplayBPMNames[] =
{
	"Actual",
	"Specified",
	"Random",
};
XToString( DisplayBPM );
LuaXType( DisplayBPM );

Steps::Steps(Song *song): m_StepsType(StepsType_Invalid), m_pSong(song),
	parent(nullptr), m_pNoteData(new NoteData), m_bNoteDataIsFilled(false),
	m_sNoteDataCompressed(""), m_sFilename(""), m_bSavedToDisk(false),
	m_LoadedFromProfile(ProfileSlot_Invalid), m_iHash(0),
	m_sDescription(""), m_sChartStyle(""),
	m_Difficulty(Difficulty_Invalid), m_iMeter(0),
	m_bAreCachedRadarValuesJustLoaded(false),
	m_sCredit(""), displayBPMType(DISPLAY_BPM_ACTUAL),
	specifiedBPMMin(0), specifiedBPMMax(0) {}

Steps::~Steps()
{
}

void Steps::GetDisplayBpms( DisplayBpms &AddTo ) const
{
	if( this->GetDisplayBPM() == DISPLAY_BPM_SPECIFIED )
	{
		AddTo.Add( this->GetMinBPM() );
		AddTo.Add( this->GetMaxBPM() );
	}
	else
	{
		float fMinBPM, fMaxBPM;
		this->GetTimingData()->GetActualBPM( fMinBPM, fMaxBPM );
		AddTo.Add( fMinBPM );
		AddTo.Add( fMaxBPM );
	}
}

bool Steps::HasAttacks() const
{
	return !this->m_Attacks.empty();
}

unsigned Steps::GetHash() const
{
	if( parent )
		return parent->GetHash();
	if( m_iHash )
		return m_iHash;
	if( m_sNoteDataCompressed.empty() )
	{
		if( !m_bNoteDataIsFilled )
			return 0; // No data, no hash.
		NoteDataUtil::GetSMNoteDataString( *m_pNoteData, m_sNoteDataCompressed );
	}
	m_iHash = GetHashForString( m_sNoteDataCompressed );
	return m_iHash;
}

bool Steps::IsNoteDataEmpty() const
{
	return this->m_sNoteDataCompressed.empty();
}

bool Steps::GetNoteDataFromSimfile()
{
	// Replace the line below with the Steps' cache file.
	std::string stepFile = this->GetFilename();
	// The code below expects lower case extensions.
	std::string extension = Rage::make_lower(GetExtension(stepFile));

	if (extension.empty() || extension == "ssc"
		|| extension == "ats") // remember cache files.
	{
		SSCLoader loader;
		if ( ! loader.LoadNoteDataFromSimfile(stepFile, *this) )
		{
			/*
			HACK: 7/20/12 -- see bugzilla #740
			users who edit songs using the ever popular .sm file
			that remove or tamper with the .ssc file later on
			complain of blank steps in the editor after reloading.
			Despite the blank steps being well justified since
			the cache files contain only the SSC step file,
			give the user some leeway and search for a .sm replacement
			*/
			SMLoader backup_loader;
			std::string transformedStepFile = stepFile;
			Rage::replace(transformedStepFile, ".ssc", ".sm");

			return backup_loader.LoadNoteDataFromSimfile(transformedStepFile, *this);
		}
		else
		{
			return true;
		}
	}
	else if (extension == "sm")
	{
		SMLoader loader;
		return loader.LoadNoteDataFromSimfile(stepFile, *this);
	}
	else if (extension == "sma")
	{
		SMALoader loader;
		return loader.LoadNoteDataFromSimfile(stepFile, *this);
	}
	else if (extension == "dwi")
	{
		return DWILoader::LoadNoteDataFromSimfile(stepFile, *this);
	}
	else if (extension == "ksf")
	{
		return KSFLoader::LoadNoteDataFromSimfile(stepFile, *this);
	}
	else if (extension == "bms" || extension == "bml" || extension == "bme" || extension == "pms")
	{
		return BMSLoader::LoadNoteDataFromSimfile(stepFile, *this);
	}
   else if (extension == "mid")
   {
      return MIDILoader::LoadNoteDataFromSimfile(stepFile, *this);
   }
   else if (extension == "chart")
   {
      return CHARTLoader::LoadNoteDataFromSimfile(stepFile, *this);
   }
	else if (extension == "edit")
	{
		// Try SSC, then fallback to SM.
		SSCLoader ldSSC;
		if(ldSSC.LoadNoteDataFromSimfile(stepFile, *this) != true)
		{
			SMLoader ldSM;
			return ldSM.LoadNoteDataFromSimfile(stepFile, *this);
		}
		else return true;
	}
	return false;
}

void Steps::SetNoteData( const NoteData& noteDataNew )
{
	ASSERT( noteDataNew.GetNumTracks() == GAMEMAN->GetStepsTypeInfo(m_StepsType).iNumTracks );

	DeAutogen( false );

	*m_pNoteData = noteDataNew;
	m_bNoteDataIsFilled = true;

	m_sNoteDataCompressed = std::string();
	m_iHash = 0;
}

void Steps::GetNoteData( NoteData& noteDataOut ) const
{
	Decompress();

	if( m_bNoteDataIsFilled )
	{
		noteDataOut = *m_pNoteData;
	}
	else
	{
		noteDataOut.ClearAll();
		noteDataOut.SetNumTracks( GAMEMAN->GetStepsTypeInfo(m_StepsType).iNumTracks );
	}
}

NoteData Steps::GetNoteData() const
{
	NoteData tmp;
	this->GetNoteData( tmp );
	return tmp;
}

void Steps::SetSMNoteData( const std::string &notes_comp_ )
{
	m_pNoteData->Init();
	m_bNoteDataIsFilled = false;

	m_sNoteDataCompressed = notes_comp_;
	m_iHash = 0;
}

/* XXX: this function should pull data from m_sFilename, like Decompress() */
void Steps::GetSMNoteData( std::string &notes_comp_out ) const
{
	if( m_sNoteDataCompressed.empty() )
	{
		if( !m_bNoteDataIsFilled )
		{
			/* no data is no data */
			notes_comp_out = "";
			return;
		}

		NoteDataUtil::GetSMNoteDataString( *m_pNoteData, m_sNoteDataCompressed );
	}

	notes_comp_out = m_sNoteDataCompressed;
}

float Steps::PredictMeter() const
{
	float pMeter = 0.775f;

	const float RadarCoeffs[NUM_RadarCategory] =
	{
		10.1f, 5.27f,-0.905f, -1.10f, 2.86f,
		0,0,0,0,0,0,0,0
	};
	const RadarValues &rv = GetRadarValues( PLAYER_1 );
	for( int r = 0; r < NUM_RadarCategory; ++r )
    {
		pMeter += rv[r] * RadarCoeffs[r];
    }
	const float DifficultyCoeffs[NUM_Difficulty] =
	{
		-0.877f, -0.877f, 0, 0.722f, 0.722f, 0
	};
	pMeter += DifficultyCoeffs[this->GetDifficulty()];

	// Init non-radar values
	const float SV = rv[RadarCategory_Stream] * rv[RadarCategory_Voltage];
	const float ChaosSquare = rv[RadarCategory_Chaos] * rv[RadarCategory_Chaos];
	pMeter += -6.35f * SV;
	pMeter += -2.58f * ChaosSquare;
	if (pMeter < 1) pMeter = 1;
	return pMeter;
}

void Steps::TidyUpData()
{
	// Don't set the StepsType to dance single if it's invalid.  That just
	// causes unrecognized charts to end up where they don't belong.
	// Leave it as StepsType_Invalid so the Song can handle it specially.  This
	// is a forwards compatibility feature, so that if a future version adds a
	// new style, editing a simfile with unrecognized Steps won't silently
	// delete them. -Kyz
	if( m_StepsType == StepsType_Invalid )
	{
		LOG->Warn("Detected steps with unknown style '%s' in '%s'", m_StepsTypeStr.c_str(), m_pSong->m_sSongFileName.c_str());
	}
	else if(m_StepsTypeStr == "")
	{
		m_StepsTypeStr= GAMEMAN->GetStepsTypeInfo(m_StepsType).stepTypeName;
	}

	if( GetDifficulty() == Difficulty_Invalid )
		SetDifficulty( StringToDifficulty(GetDescription()) );

	if( GetDifficulty() == Difficulty_Invalid )
	{
		if(	 GetMeter() == 1 )	SetDifficulty( Difficulty_Beginner );
		else if( GetMeter() <= 3 )	SetDifficulty( Difficulty_Easy );
		else if( GetMeter() <= 6 )	SetDifficulty( Difficulty_Medium );
		else				SetDifficulty( Difficulty_Hard );
	}

	if( GetMeter() < 1) // meter is invalid
		SetMeter( int(PredictMeter()) );
}

void Steps::CalculateRadarValues( float fMusicLengthSeconds )
{
	using std::min;
	// If we're autogen, don't calculate values.  GetRadarValues will take from our parent.
	if( parent != nullptr )
		return;

	if( m_bAreCachedRadarValuesJustLoaded )
	{
		m_bAreCachedRadarValuesJustLoaded = false;
		return;
	}

	// Do write radar values, and leave it up to the reading app whether they want to trust
	// the cached values without recalculating them.
	/*
	// If we're an edit, leave the RadarValues invalid.
	if( IsAnEdit() )
		return;
	*/

	NoteData tempNoteData;
	this->GetNoteData( tempNoteData );

	FOREACH_PlayerNumber( pn )
		m_CachedRadarValues[pn].Zero();

	GAMESTATE->SetProcessedTimingData(this->GetTimingData());
	if( tempNoteData.IsComposite() )
	{
		vector<NoteData> vParts;

		NoteDataUtil::SplitCompositeNoteData( tempNoteData, vParts );
		for( size_t pn = 0; pn < min(vParts.size(), size_t(NUM_PLAYERS)); ++pn )
        {
			NoteDataUtil::CalculateRadarValues( vParts[pn], fMusicLengthSeconds, m_CachedRadarValues[pn] );
        }
    }
	else if (GAMEMAN->GetStepsTypeInfo(this->m_StepsType).m_StepsTypeCategory == StepsTypeCategory_Couple)
	{
		NoteData p1 = tempNoteData;
		// XXX: Assumption that couple will always have an even number of notes.
		const int tracks = tempNoteData.GetNumTracks() / 2;
		p1.SetNumTracks(tracks);
		NoteDataUtil::CalculateRadarValues(p1,
										   fMusicLengthSeconds,
										   m_CachedRadarValues[PLAYER_1]);
		// at this point, p2 is tempNoteData.
		NoteDataUtil::ShiftTracks(tempNoteData, tracks);
		tempNoteData.SetNumTracks(tracks);
		NoteDataUtil::CalculateRadarValues(tempNoteData,
										   fMusicLengthSeconds,
										   m_CachedRadarValues[PLAYER_2]);
	}
	else
	{
		NoteDataUtil::CalculateRadarValues( tempNoteData, fMusicLengthSeconds, m_CachedRadarValues[0] );
		std::fill_n( m_CachedRadarValues + 1, NUM_PLAYERS-1, m_CachedRadarValues[0] );
	}
	GAMESTATE->SetProcessedTimingData(nullptr);
}

void Steps::ChangeFilenamesForCustomSong()
{
	m_sFilename= custom_songify_path(m_sFilename);
	if(!m_MusicFile.empty())
	{
		m_MusicFile= custom_songify_path(m_MusicFile);
	}
}

void Steps::Decompress() const
{
	const_cast<Steps *>(this)->Decompress();
}

bool stepstype_is_kickbox(StepsType st)
{
	return st == StepsType_kickbox_human || st == StepsType_kickbox_quadarm ||
		st == StepsType_kickbox_insect || st == StepsType_kickbox_arachnid;
}

void Steps::Decompress()
{
	if( m_bNoteDataIsFilled )
		return;	// already decompressed

	if( parent )
	{
		// get autogen m_pNoteData
		NoteData notedata;
		parent->GetNoteData( notedata );

		m_bNoteDataIsFilled = true;

		int iNewTracks = GAMEMAN->GetStepsTypeInfo(m_StepsType).iNumTracks;

		if( this->m_StepsType == StepsType_lights_cabinet )
		{
			NoteDataUtil::LoadTransformedLights( notedata, *m_pNoteData, iNewTracks );
		}
		else
		{
			// Special case so that kickbox can have autogen steps that are playable.
			// Hopefully I'll replace this with a good generalized autogen system
			// later.  -Kyz
			if(stepstype_is_kickbox(this->m_StepsType))
			{
				// Number of notes seems like a useful "random" input so that charts
				// from different sources come out different, but autogen always
				// makes the same thing from one source. -Kyz
				NoteDataUtil::AutogenKickbox(notedata, *m_pNoteData, *GetTimingData(),
					this->m_StepsType,
					static_cast<int>(GetRadarValues(PLAYER_1)[RadarCategory_TapsAndHolds]));
			}
			else
			{
				NoteDataUtil::LoadTransformedSlidingWindow( notedata, *m_pNoteData, iNewTracks );

				NoteDataUtil::RemoveStretch( *m_pNoteData, m_StepsType );
			}
		}
		return;
	}

	if( !m_sFilename.empty() && m_sNoteDataCompressed.empty() )
	{
		// We have NoteData on disk and not in memory. Load it.
		if (!this->GetNoteDataFromSimfile())
		{
			LOG->Warn("Couldn't load the %s chart's NoteData from \"%s\"",
					  DifficultyToString(m_Difficulty).c_str(), m_sFilename.c_str());
			return;
		}

		this->GetSMNoteData( m_sNoteDataCompressed );
	}

	if( m_sNoteDataCompressed.empty() )
	{
		/* there is no data, do nothing */
	}
	else
	{
		// load from compressed
		bool bComposite = GAMEMAN->GetStepsTypeInfo(m_StepsType).m_StepsTypeCategory == StepsTypeCategory_Routine;
		m_bNoteDataIsFilled = true;
		m_pNoteData->SetNumTracks( GAMEMAN->GetStepsTypeInfo(m_StepsType).iNumTracks );

		NoteDataUtil::LoadFromSMNoteDataString( *m_pNoteData, m_sNoteDataCompressed, bComposite );
	}
}

void Steps::Compress() const
{
	// Always leave lights data uncompressed.
	if( this->m_StepsType == StepsType_lights_cabinet && m_bNoteDataIsFilled )
	{
		m_sNoteDataCompressed = std::string();
		return;
	}

	// Don't compress data in the editor: it's still in use.
	if (GAMESTATE->m_bInStepEditor)
	{
		return;
	}

	if( !m_sFilename.empty() && m_LoadedFromProfile == ProfileSlot_Invalid )
	{
		/* We have a file on disk; clear all data in memory.
		 * Data on profiles can't be accessed normally (need to mount and time-out
		 * the device), and when we start a game and load edits, we want to be
		 * sure that it'll be available if the user picks it and pulls the device.
		 * Also, Decompress() doesn't know how to load .edits. */
		m_pNoteData->Init();
		m_bNoteDataIsFilled = false;

		/* Be careful; 'x = ""', m_sNoteDataCompressed.clear() and m_sNoteDataCompressed.reserve(0)
		 * don't always free the allocated memory. */
		m_sNoteDataCompressed = std::string();
		return;
	}

	// We have no file on disk. Compress the data, if necessary.
	if( m_sNoteDataCompressed.empty() )
	{
		if( !m_bNoteDataIsFilled )
			return; /* no data is no data */
		NoteDataUtil::GetSMNoteDataString( *m_pNoteData, m_sNoteDataCompressed );
	}

	m_pNoteData->Init();
	m_bNoteDataIsFilled = false;
}

/* Copy our parent's data. This is done when we're being changed from autogen
 * to normal. (needed?) */
void Steps::DeAutogen( bool bCopyNoteData )
{
	if( !parent )
		return; // OK

	if( bCopyNoteData )
		Decompress();	// fills in m_pNoteData with sliding window transform

	m_sDescription		= Real()->m_sDescription;
	m_sChartStyle		= Real()->m_sChartStyle;
	m_Difficulty		= Real()->m_Difficulty;
	m_iMeter		= Real()->m_iMeter;
	std::copy( Real()->m_CachedRadarValues, Real()->m_CachedRadarValues + NUM_PLAYERS, m_CachedRadarValues );
	m_sCredit		= Real()->m_sCredit;
	parent = nullptr;

	if( bCopyNoteData )
		Compress();
}

void Steps::AutogenFrom( const Steps *parent_, StepsType ntTo )
{
	parent = parent_;
	m_StepsType = ntTo;
	m_StepsTypeStr= GAMEMAN->GetStepsTypeInfo(ntTo).stepTypeName;
	m_Timing = parent->m_Timing;
}

void Steps::CopyFrom( Steps* pSource, StepsType ntTo, float fMusicLengthSeconds )	// pSource does not have to be of the same StepsType
{
	m_StepsType = ntTo;
	m_StepsTypeStr= GAMEMAN->GetStepsTypeInfo(ntTo).stepTypeName;
	NoteData noteData;
	pSource->GetNoteData( noteData );
	noteData.SetNumTracks( GAMEMAN->GetStepsTypeInfo(ntTo).iNumTracks );
	parent = nullptr;
	m_Timing = pSource->m_Timing;
	this->m_pSong = pSource->m_pSong;
	this->m_Attacks = pSource->m_Attacks;
	this->m_sAttackString = pSource->m_sAttackString;
	this->SetNoteData( noteData );
	this->SetDescription( pSource->GetDescription() );
	this->SetDifficulty( pSource->GetDifficulty() );
	this->SetMeter( pSource->GetMeter() );
	this->CalculateRadarValues( fMusicLengthSeconds );
}

void Steps::CreateBlank( StepsType ntTo )
{
	m_StepsType = ntTo;
	m_StepsTypeStr= GAMEMAN->GetStepsTypeInfo(ntTo).stepTypeName;
	NoteData noteData;
	noteData.SetNumTracks( GAMEMAN->GetStepsTypeInfo(ntTo).iNumTracks );
	this->SetNoteData( noteData );
}

void Steps::SetDifficultyAndDescription( Difficulty dc, std::string sDescription )
{
	DeAutogen();
	m_Difficulty = dc;
	m_sDescription = sDescription;
	if( GetDifficulty() == Difficulty_Edit )
		MakeValidEditDescription( m_sDescription );
}

void Steps::SetCredit( std::string sCredit )
{
	DeAutogen();
	m_sCredit = sCredit;
}

void Steps::SetChartStyle( std::string sChartStyle )
{
	DeAutogen();
	m_sChartStyle = sChartStyle;
}

bool Steps::MakeValidEditDescription( std::string &sPreferredDescription )
{
	if( int(sPreferredDescription.size()) > MAX_STEPS_DESCRIPTION_LENGTH )
	{
		sPreferredDescription = Rage::head(sPreferredDescription, MAX_STEPS_DESCRIPTION_LENGTH);
		return true;
	}
	return false;
}

void Steps::SetMeter( int meter )
{
	DeAutogen();
	m_iMeter = meter;
}

const TimingData *Steps::GetTimingData() const
{
	return m_Timing.empty() ? &m_pSong->m_SongTiming : &m_Timing;
}

bool Steps::HasSignificantTimingChanges() const
{
	const TimingData *timing = GetTimingData();
	if( timing->HasStops() || timing->HasDelays() || timing->HasWarps() ||
		timing->HasSpeedChanges() || timing->HasScrollChanges() )
		return true;

	if( timing->HasBpmChanges() )
	{
		// check to see if these changes are significant.
		if( (GetMaxBPM() - GetMinBPM()) > 3.000f )
			return true;
	}

	return false;
}

const std::string Steps::GetMusicPath() const
{
	return Song::GetSongAssetPath(
		m_MusicFile.empty() ? m_pSong->m_sMusicFile : m_MusicFile,
		m_pSong->GetSongDir());
}

const std::string& Steps::GetMusicFile() const
{
	return m_MusicFile;
}

void Steps::SetMusicFile(const std::string& file)
{
	m_MusicFile= file;
}

void Steps::SetCachedRadarValues( const RadarValues v[NUM_PLAYERS] )
{
	DeAutogen();
	std::copy( v, v + NUM_PLAYERS, m_CachedRadarValues );
	m_bAreCachedRadarValuesJustLoaded = true;
}

// lua start
#include "LuaBinding.h"
/** @brief Allow Lua to have access to the Steps. */
class LunaSteps: public Luna<Steps>
{
public:
	DEFINE_METHOD( GetStepsType,	m_StepsType )
	DEFINE_METHOD( GetDifficulty,	GetDifficulty() )
	DEFINE_METHOD( GetDescription,	GetDescription() )
	DEFINE_METHOD( GetChartStyle,	GetChartStyle() )
	DEFINE_METHOD( GetAuthorCredit, GetCredit() )
	DEFINE_METHOD( GetMeter,	GetMeter() )
	DEFINE_METHOD( GetFilename,	GetFilename() )
	DEFINE_METHOD( IsAutogen,	IsAutogen() )
	DEFINE_METHOD( IsAnEdit,	IsAnEdit() )
	DEFINE_METHOD( IsAPlayerEdit,	IsAPlayerEdit() )

	static int HasSignificantTimingChanges( T* p, lua_State *L )
	{
		lua_pushboolean(L, p->HasSignificantTimingChanges());
		return 1;
	}
	static int HasAttacks( T* p, lua_State *L )
	{
		lua_pushboolean(L, p->HasAttacks());
		return 1;
	}
	static int GetRadarValues( T* p, lua_State *L )
	{
		PlayerNumber pn = PLAYER_1;
		if (!lua_isnil(L, 1)) {
			pn = Enum::Check<PlayerNumber>(L, 1);
		}

		RadarValues &rv = const_cast<RadarValues &>(p->GetRadarValues(pn));
		rv.PushSelf(L);
		return 1;
	}
	static int GetTimingData( T* p, lua_State *L )
	{
		p->GetTimingData()->PushSelf(L);
		return 1;
	}
	static int GetHash( T* p, lua_State *L ) { lua_pushnumber( L, p->GetHash() ); return 1; }
	// untested
	/*
	static int GetSMNoteData( T* p, lua_State *L )
	{
		std::string out;
		p->GetSMNoteData( out );
		lua_pushstring( L, out.c_str() );
		return 1;
	}
	*/
	static int GetChartName(T *p, lua_State *L)
	{
		lua_pushstring(L, p->GetChartName().c_str());
		return 1;
	}
	static int GetDisplayBpms( T* p, lua_State *L )
	{
		DisplayBpms temp;
		p->GetDisplayBpms(temp);
		float fMin = temp.GetMin();
		float fMax = temp.GetMax();
		vector<float> fBPMs;
		fBPMs.push_back( fMin );
		fBPMs.push_back( fMax );
		LuaHelpers::CreateTableFromArray(fBPMs, L);
		return 1;
	}
	static int IsDisplayBpmSecret( T* p, lua_State *L )
	{
		DisplayBpms temp;
		p->GetDisplayBpms(temp);
		lua_pushboolean( L, temp.IsSecret() );
		return 1;
	}
	static int IsDisplayBpmConstant( T* p, lua_State *L )
	{
		DisplayBpms temp;
		p->GetDisplayBpms(temp);
		lua_pushboolean( L, temp.BpmIsConstant() );
		return 1;
	}
	static int IsDisplayBpmRandom( T* p, lua_State *L )
	{
		lua_pushboolean( L, p->GetDisplayBPM() == DISPLAY_BPM_RANDOM );
		return 1;
	}
	DEFINE_METHOD( PredictMeter, PredictMeter() )
	static int GetDisplayBPMType( T* p, lua_State *L )
	{
		LuaHelpers::Push( L, p->GetDisplayBPM() );
		return 1;
	}
	static int count_notes_in_columns(T* p, lua_State* L)
	{
		NoteData note_data;
		p->GetNoteData(note_data);
		vector<std::map<TapNoteType, int> > note_counts;
		vector<std::map<TapNoteSubType, float> > hold_durations;
		note_data.count_notes_in_columns(p->GetTimingData(), note_counts, hold_durations);
		// Each element of note_counts is the data for one column.
		// Each element in a column is the count for a given TapNoteType.
		// hold_durations has a similar structure for the total duration of each
		// hold subtype.
		lua_createtable(L, note_counts.size(), 0);
		for(size_t column= 0; column < note_counts.size(); ++column)
		{
			auto& column_entry= note_counts[column];
			auto& durr_entry= hold_durations[column];
			lua_createtable(L, 0, column_entry.size() + durr_entry.size());
			for(auto&& tap_entry : column_entry)
			{
				Enum::Push(L, tap_entry.first);
				lua_pushnumber(L, tap_entry.second);
				lua_settable(L, -3);
			}
			for(auto&& hold_entry : durr_entry)
			{
				Enum::Push(L, hold_entry.first);
				lua_pushnumber(L, hold_entry.second);
				lua_settable(L, -3);
			}
			lua_rawseti(L, -2, column+1);
		}
		return 1;
	}

	LunaSteps()
	{
		ADD_METHOD( GetAuthorCredit );
		ADD_METHOD( GetChartStyle );
		ADD_METHOD( GetDescription );
		ADD_METHOD( GetDifficulty );
		ADD_METHOD( GetFilename );
		ADD_METHOD( GetHash );
		ADD_METHOD( GetMeter );
		ADD_METHOD( HasSignificantTimingChanges );
		ADD_METHOD( HasAttacks );
		ADD_METHOD( GetRadarValues );
		ADD_METHOD( GetTimingData );
		ADD_METHOD( GetChartName );
		//ADD_METHOD( GetSMNoteData );
		ADD_METHOD( GetStepsType );
		ADD_METHOD( IsAnEdit );
		ADD_METHOD( IsAutogen );
		ADD_METHOD( IsAPlayerEdit );
		ADD_METHOD( GetDisplayBpms );
		ADD_METHOD( IsDisplayBpmSecret );
		ADD_METHOD( IsDisplayBpmConstant );
		ADD_METHOD( IsDisplayBpmRandom );
		ADD_METHOD( PredictMeter );
		ADD_METHOD( GetDisplayBPMType );
		ADD_METHOD(count_notes_in_columns);
	}
};

LUA_REGISTER_CLASS( Steps )
// lua end


/*
 * (c) 2001-2004 Chris Danford, Glenn Maynard, David Wilson
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, and/or sell copies of the Software, and to permit persons to
 * whom the Software is furnished to do so, provided that the above
 * copyright notice(s) and this permission notice appear in all copies of
 * the Software and that both the above copyright notice(s) and this
 * permission notice appear in supporting documentation.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF
 * THIRD PARTY RIGHTS. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR HOLDERS
 * INCLUDED IN THIS NOTICE BE LIABLE FOR ANY CLAIM, OR ANY SPECIAL INDIRECT
 * OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 * OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */
