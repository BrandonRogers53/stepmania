#include "global.h"
/*
-----------------------------------------------------------------------------
 Class: Steps

 Desc: See header.

 Copyright (c) 2001-2002 by the person(s) listed below.  All rights reserved.
	Chris Danford
	Glenn Maynard
-----------------------------------------------------------------------------
*/

#include "Steps.h"
#include "song.h"
#include "Steps.h"
#include "IniFile.h"
#include "math.h"	// for fabs()
#include "RageUtil.h"
#include "RageLog.h"
#include "NoteData.h"
#include "GameInput.h"
#include "RageException.h"
#include "MsdFile.h"
#include "GameManager.h"
#include "NoteDataUtil.h"


Steps::Steps()
{
	/* FIXME: should we init this to STEPS_TYPE_INVALID? 
	 * I have a feeling that it's the right thing to do but that
	 * it'd trip obscure asserts all over the place, so I'll wait
	 * until after b6 to do this. -glenn */
	m_StepsType = STEPS_TYPE_DANCE_SINGLE;
	m_Difficulty = DIFFICULTY_INVALID;
	m_iMeter = 0;
	for(int i = 0; i < NUM_RADAR_CATEGORIES; ++i)
		m_fRadarValues[i] = -1; /* unknown */

	notes = NULL;
	notes_comp = NULL;
	parent = NULL;
}

Steps::~Steps()
{
	delete notes;
	delete notes_comp;
}

void Steps::SetNoteData( const NoteData* pNewNoteData )
{
	ASSERT( pNewNoteData->GetNumTracks() == GameManager::NotesTypeToNumTracks(m_StepsType) );

	DeAutogen();

	delete notes_comp;
	notes_comp = NULL;

	delete notes;
	notes = new NoteData(*pNewNoteData);
}

void Steps::GetNoteData( NoteData* pNoteDataOut ) const
{
	ASSERT(this);
	ASSERT(pNoteDataOut);

	Decompress();

	if( notes != NULL )
		*pNoteDataOut = *notes;
	else
	{
		pNoteDataOut->ClearAll();
		pNoteDataOut->SetNumTracks( GameManager::NotesTypeToNumTracks(m_StepsType) );
	}
}

void Steps::SetSMNoteData( const CString &out )
{
	delete notes;
	notes = NULL;

	if(!notes_comp)
		notes_comp = new CString;

	*notes_comp = out;
}

CString Steps::GetSMNoteData() const
{
	if(!notes_comp)
	{
		if(!notes) return ""; /* no data is no data */
		notes_comp = new CString;
		NoteDataUtil::GetSMNoteDataString( *notes, *notes_comp );
	}

	return *notes_comp;
}

void Steps::TidyUpData()
{
	if( GetDifficulty() == DIFFICULTY_INVALID )
		SetDifficulty(StringToDifficulty(GetDescription()));
	
	if( GetDifficulty() == DIFFICULTY_INVALID )
	{
		if(		 GetMeter() == 1 )	SetDifficulty(DIFFICULTY_BEGINNER);
		else if( GetMeter() <= 3 )	SetDifficulty(DIFFICULTY_EASY);
		else if( GetMeter() <= 6 )	SetDifficulty(DIFFICULTY_MEDIUM);
		else						SetDifficulty(DIFFICULTY_HARD);
	}
	// Meter is overflowing (invalid), but some files (especially maniac/smaniac steps) are purposefully set higher than 10.
	// See: BMR's Gravity; we probably should keep those as difficult as we can represent.
	/* Why? If the data file says a meter of 72, we should keep it as 72; if
	 * individual bits of code (eg. scoring, feet) have maximums, they should
	 * enforce it internally.  Doing it here will make us lose the difficulty
	 * completely if the song is edited and written. -glenn */
/*	if( GetMeter() >10 ) {
			if( GetDifficulty() == DIFFICULTY_HARD || GetDifficulty() == DIFFICULTY_CHALLENGE)
				SetMeter(10);
			else
				SetMeter(0);
	} */
	if( GetMeter() < 1) // meter is invalid
	{
		// guess meter from difficulty class
		switch( GetDifficulty() )
		{
		case DIFFICULTY_BEGINNER:	SetMeter(1);	break;
		case DIFFICULTY_EASY:		SetMeter(3);	break;
		case DIFFICULTY_MEDIUM:		SetMeter(5);	break;
		case DIFFICULTY_HARD:		SetMeter(8);	break;
		case DIFFICULTY_CHALLENGE:	SetMeter(8);	break;
		case DIFFICULTY_INVALID:	SetMeter(5);	break;
		default:	ASSERT(0);
		}
	}

	if( m_sDescription.empty() )
	{
		m_sDescription = Capitalize( DifficultyToString(m_Difficulty) );
	}
}

void Steps::Decompress() const
{
	if(notes)
	{
		return;	// already decompressed
	}
	else if(parent)
	{
		// get autogen notes
		NoteData pdata;
		parent->GetNoteData(&pdata);

		notes = new NoteData;
		// MD 10/29/03 - add track-combining logic
		int iNewTracks = GameManager::NotesTypeToNumTracks(m_StepsType);
		// ...if we don't set it to SOMETHING, we get no steps.
		// This breaks autogen dead. :->
		notes->SetNumTracks( pdata.GetNumTracks() );
		notes->CopyRange( &pdata, 0, pdata.GetLastRow(), 0 );
		if( pdata.GetNumTracks() > iNewTracks)
		{
			int iOriginalTracks = pdata.GetNumTracks();
			int iUnevenTracks = iOriginalTracks % iNewTracks;
			int iTracksToOverlap = iOriginalTracks / iNewTracks;
			if( iTracksToOverlap ) {
				// if we have at least as many tracks in the old mode
				// as we do in the mode we're going to
				for (int ix = 0; ix < iNewTracks; ix++)
				{
					for (int iy = 0; iy < iTracksToOverlap; iy++)
					{
						notes->CombineTracks(ix, (ix + iy * iNewTracks));
					}
				}
				if( iUnevenTracks ) {
					for (int ix = iOriginalTracks - iUnevenTracks;
						 ix < iOriginalTracks;
						 ix++)
						 {
							 // spread out the remaining tracks evenly
							 notes->CombineTracks((ix * iOriginalTracks) % iNewTracks, ix);
						 }
				}
			}
		} else
			notes->LoadTransformedSlidingWindow( &pdata, iNewTracks );
		notes->SetNumTracks( GameManager::NotesTypeToNumTracks(m_StepsType) );
		// end MD 10/29/03
		NoteDataUtil::FixImpossibleRows( *notes, m_StepsType );
	}
	else if(!notes_comp)
	{
		/* there is no data, do nothing */
	}
	else
	{
		// load from compressed
		notes = new NoteData;
		notes->SetNumTracks( GameManager::NotesTypeToNumTracks(m_StepsType) );

		NoteDataUtil::LoadFromSMNoteDataString(*notes, *notes_comp);
	}
}

void Steps::Compress() const
{
	if(!notes_comp)
	{
		if(!notes) return; /* no data is no data */
		notes_comp = new CString;
		NoteDataUtil::GetSMNoteDataString( *notes, *notes_comp );
	}

	delete notes;
	notes = NULL;
}

/* Copy our parent's data.  This is done when we're being changed from autogen
 * to normal. (needed?) */
void Steps::DeAutogen()
{
	if(!parent)
		return; /* OK */

	Decompress();	// fills in notes with sliding window transform

	m_iMeter		= Real()->m_iMeter;
	m_sDescription	= Real()->m_sDescription;
	m_Difficulty	= Real()->m_Difficulty;
	for(int i = 0; i < NUM_RADAR_CATEGORIES; ++i)
		m_fRadarValues[i] = Real()->m_fRadarValues[i];

	parent = NULL;

	Compress();
}

void Steps::AutogenFrom( Steps *parent_, StepsType ntTo )
{
	parent = parent_;
	m_StepsType = ntTo;
}

void Steps::CopyFrom( Steps* pSource, StepsType ntTo )	// pSource does not have to be of the same StepsType!
{
	m_StepsType = ntTo;
	NoteData noteData;
	pSource->GetNoteData( &noteData );
	noteData.SetNumTracks( GameManager::NotesTypeToNumTracks(ntTo) ); 
	this->SetNoteData( &noteData );
	this->SetDescription( "Copied from "+pSource->GetDescription() );
	this->SetDifficulty( pSource->GetDifficulty() );
	this->SetMeter( pSource->GetMeter() );

	const float* radarValues = pSource->GetRadarValues();
	for( int r=0; r<NUM_RADAR_CATEGORIES; r++ )
		this->SetRadarValue( (RadarCategory)r, radarValues[r] );
}

void Steps::CreateBlank( StepsType ntTo )
{
	m_StepsType = ntTo;
	NoteData noteData;
	noteData.SetNumTracks( GameManager::NotesTypeToNumTracks(ntTo) );
	this->SetNoteData( &noteData );
}


const Steps *Steps::Real() const
{
	if(parent) return parent;
	return this;
}

bool Steps::IsAutogen() const
{
	return parent != NULL;
}

void Steps::SetDescription(CString desc)
{
	DeAutogen();
	m_sDescription = desc;
}

void Steps::SetDifficulty(Difficulty d)
{
	DeAutogen();
	m_Difficulty = d;
}

void Steps::SetMeter(int meter)
{
	DeAutogen();
	m_iMeter = meter;
}

void Steps::SetRadarValue(int r, float val)
{
	DeAutogen();
	ASSERT(r < NUM_RADAR_CATEGORIES);
	m_fRadarValues[r] = val;
}


//
// Sorting stuff
//

bool CompareNotesPointersByRadarValues(const Steps* pNotes1, const Steps* pNotes2)
{
	float fScore1 = 0;
	float fScore2 = 0;
	
	for( int r=0; r<NUM_RADAR_CATEGORIES; r++ )
	{
		fScore1 += pNotes1->GetRadarValues()[r];
		fScore2 += pNotes2->GetRadarValues()[r];
	}

	return fScore1 < fScore2;
}

bool CompareNotesPointersByMeter(const Steps *pNotes1, const Steps* pNotes2)
{
	return pNotes1->GetMeter() < pNotes2->GetMeter();
}

bool CompareNotesPointersByDifficulty(const Steps *pNotes1, const Steps *pNotes2)
{
	return pNotes1->GetDifficulty() < pNotes2->GetDifficulty();
}

void SortNotesArrayByDifficulty( vector<Steps*> &arraySteps )
{
	/* Sort in reverse order of priority. */
	stable_sort( arraySteps.begin(), arraySteps.end(), CompareNotesPointersByRadarValues );
	stable_sort( arraySteps.begin(), arraySteps.end(), CompareNotesPointersByMeter );
	stable_sort( arraySteps.begin(), arraySteps.end(), CompareNotesPointersByDifficulty );
}



bool Steps::MemCardData::HighScore::operator>=( const Steps::MemCardData::HighScore& other ) const
{
	return iScore >= other.iScore;
	/* Make sure we treat AAAA as higher than AAA, even though the score
		* is the same. 
		*
		* XXX: Isn't it possible to beat the grade but not beat the score, since
		* grading and scores are on completely different systems?  Should we be
		* checking for these completely separately? */
	//	if( vsScore > this->fScore )
	//		return true;
	//	if( vsScore < this->fScore )
	//		return false;
	//	return vsGrade > this->grade;
}

void Steps::MemCardData::AddHighScore( Steps::MemCardData::HighScore hs, int &iIndexOut )
{
	int i;
	for( i=0; i<(int)vHighScores.size(); i++ )
	{
		if( hs >= vHighScores[i] )	// tie goes to new score
			break;
	}

	if( i < NUM_RANKING_LINES )
	{
		vHighScores.insert( vHighScores.begin()+i, hs );
		iIndexOut = i;
		if( vHighScores.size() > NUM_RANKING_LINES )
			vHighScores.erase( vHighScores.begin()+NUM_RANKING_LINES, vHighScores.end() );
	}
}
