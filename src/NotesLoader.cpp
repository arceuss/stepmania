#include "global.h"
#include "NotesLoader.h"

#include <array>

#include "NotesLoaderSM.h"
#include "NotesLoaderSMA.h"
#include "NotesLoaderSSC.h"
#include "NotesLoaderDWI.h"
#include "NotesLoaderBMS.h"
#include "NotesLoaderKSF.h"
#include "NotesLoaderChart.h"
#include "NotesLoaderMID.h"
#include "RageUtil.h"

using std::vector;
using std::string;

void NotesLoader::GetMainAndSubTitlesFromFullTitle( const std::string &sFullTitle, std::string &sMainTitleOut, std::string &sSubTitleOut )
{
	std::array<std::string, 5> sLeftSeps =
	{
		{
			"\t", " -", " ~", " (", " ["
		}
	};

	for (auto const &sep: sLeftSeps)
	{
		size_t iBeginIndex = sFullTitle.find( sep );
		if( iBeginIndex == string::npos )
			continue;
		sMainTitleOut = Rage::head(sFullTitle, iBeginIndex);
		sSubTitleOut = sFullTitle.substr( iBeginIndex+1, sFullTitle.size()-iBeginIndex+1 );
		return;
	}
	sMainTitleOut = sFullTitle;
	sSubTitleOut = "";
};

bool NotesLoader::LoadFromDir( const std::string &sPath, Song &out, std::set<std::string> &BlacklistedImages, bool load_autosave )
{
	vector<std::string> list;

	BlacklistedImages.clear();
	SSCLoader loaderSSC;
	loaderSSC.GetApplicableFiles( sPath, list, load_autosave );
	if( !list.empty() )
	{
		if( !loaderSSC.LoadFromDir( sPath, out, load_autosave ) )
		{
			return false;
		}
		return true;
	}
	SMALoader loaderSMA;
	loaderSMA.GetApplicableFiles( sPath, list );
	if (!list.empty() )
	{
		return loaderSMA.LoadFromDir( sPath, out );
	}
	SMLoader loaderSM;
	loaderSM.GetApplicableFiles( sPath, list );
	if (!list.empty() )
	{
		return loaderSM.LoadFromDir( sPath, out );
	}
	DWILoader::GetApplicableFiles( sPath, list );
	if( !list.empty() )
	{
		return DWILoader::LoadFromDir( sPath, out, BlacklistedImages );
	}
	BMSLoader::GetApplicableFiles( sPath, list );
	if( !list.empty() )
		return BMSLoader::LoadFromDir( sPath, out );
	/*
	PMSLoader::GetApplicableFiles( sPath, list );
	if( !list.empty() )
	{
		return PMSLoader::LoadFromDir( sPath, out );
	}
	*/
	KSFLoader::GetApplicableFiles( sPath, list );
	if( !list.empty() )
	{
		return KSFLoader::LoadFromDir( sPath, out );
	}
   MIDILoader::GetApplicableFiles( sPath, list );
   if( !list.empty() )
      return MIDILoader::LoadFromDir( sPath, out );
   CHARTLoader::GetApplicableFiles( sPath, list );
   if( !list.empty() )
      return CHARTLoader::LoadFromDir( sPath, out );
	return false;
}


/*
 * (c) 2001-2004,2007 Chris Danford, Glenn Maynard, Steve Checkoway
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
