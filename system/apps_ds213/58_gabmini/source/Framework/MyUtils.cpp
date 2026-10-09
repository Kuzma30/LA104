#include "MyUtils.h"
#include "F2D.h"

namespace CUtils
{
extern char tmp[16];

/*static*/ char* FormatFrequency( float fF, int nChars )
{
	char* strUnits = (char*)" Hz";
	if (fF >= 999.9995f)
	{
		strUnits = (char*)" kHz";
		fF *= 0.001f;
	}
	if (fF >= 999.9995f)
	{
		strUnits = (char*)" MHz";
		fF *= 0.001f;
	}
	// printf truncates the 3 decimals, round instead
	fF += 0.0005f;
	sprintf( tmp, "%f", F2D(fF) );
	int nLen = strlen(tmp);
	int nLenUnits = strlen(strUnits);
	while ( nLen + nLenUnits > nChars )
		tmp[--nLen] = 0;
	if ( tmp[nLen-1] == '.' )
		tmp[--nLen] = 0;
	strcat( tmp, strUnits );
	return tmp;
}

/*static*/ char* FormatTime( float fT, int nChars )
{
	char* strUnits = (char*)" s";

	if (fT < 0.001f)
	{
		strUnits = (char*)" \xe6s";
		fT *= 1000.0f;
	} else
	if (fT < 1.0f)
	{
		strUnits = (char*)" ms";
		fT *= 1000.0f;
	}

	sprintf( tmp, "%f", F2D(fT) );
	int nLen = strlen(tmp);
	int nLenUnits = strlen(strUnits);
	while ( nLen + nLenUnits > nChars )
		tmp[--nLen] = 0;
	if ( tmp[nLen-1] == '.' )
		tmp[--nLen] = 0;
	strcat( tmp, strUnits );
	return tmp;
}

}