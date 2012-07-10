#include "GUIText.h"

GUIText::GUIText( char* s , ID3DXFont* f )
{
	static char* colorsName[]={"red","blue","green","pink","black","white"};
	static int colorsHex[]={0xFFFF0000,0xFF0000FF,0xFF00FF00,0xFFFF6EC7,0xFFFFFFFF,0xFF000000};

	static int predefinedColorsHex[]={0xFFFF0000,0xFF0000FF,0xFF00FF00,0xFFFF6EC7,0xFFFFFFFF,0xFF000000};

	int lastIndex=0;

	font=f;
	if(strlen(s)>=sizeof(str))
	{
		//Error!!
		strncpy(str,s,sizeof(str)-5);
		strcat(str,"[..]");
	}
	else
		strcpy(str,s);

	for(int i=0;i<strlen(str);i++)
	{
		if(str[i]=='$')
		{
			bool done=false;
			for(int k=0;k<(sizeof(colorsName)/sizeof(char*));k++)
			{
				if(strncmp((str+i+1),colorsName[k],strlen(colorsName[k]))==0)
				{
					//Color found
					GUIColorChunk tmp;
					tmp.color=colorsHex[k];
					tmp.start=lastIndex;
					tmp.end=i-1;
					lastIndex=strlen(colorsName[k])+1+i;
					done=true;
					textChunks.push_back((tmp));
					break;
				}
			}
			if(done)
			{

			}
			else
				if(IsHex(str[i+1])&&!IsHex(str[i+2]))
				{
					//Predefined colors (1,2,3 etc.)
					GUIColorChunk tmp;
					tmp.color=predefinedColorsHex[str[i+1]-'0'];
					tmp.start=lastIndex;
					tmp.end=i-1;
					lastIndex=i+1+1;
					textChunks.push_back((tmp));
				}
			else
				if(IsHex(str[i+1])&&IsHex(str[i+2])&&IsHex(str[i+3])&&!IsHex(str[i+4]))
				{
					//3 byte hex colors
					/*GUIColorChunk tmp;
					tmp.color=predefinedColorsHex[str[i+1]-'0'];
					tmp.start=lastIndex;
					tmp.end=i-1;
					lastIndex=i+1+1;*/
				}
		}
	}
	{
		GUIColorChunk tmp;
		tmp.color=0xFFFFFFFF;
		tmp.start=lastIndex;
		tmp.end=strlen(str);
		textChunks.push_back((tmp));
	}
	/*
	1 - Shift colors right by 1
	*/
	for(int i=textChunks.size()-2;i>=0;i--)
	{
		textChunks[i+1].color=textChunks[i].color;
	}
	/*
	2 - Calculate offsets
	*/
	for(int i=0;i<textChunks.size();i++)
	{
		char tmp[512];
		memset(tmp,0,sizeof(tmp));
		RECT font_rect;
		font_rect.left=0;
		font_rect.right=500;
		font_rect.top=0;
		font_rect.bottom=200;


		
		if(i>0)
		{
			strncpy(tmp,str+textChunks[i-1].start,textChunks[i-1].end-textChunks[i-1].start);
			font->DrawTextA(NULL,tmp,-1,&font_rect,DT_CALCRECT|DT_LEFT|DT_TOP,textChunks[i].color);

			textChunks[i].offsetX=textChunks[i-1].offsetX+font_rect.right;
		}
		else
		{
			
			textChunks[i].color=0xFF000000;
			textChunks[i].offsetX=0;
		}

		//TODO:Calculate top offset for line breaks
		textChunks[i].offsetY=0;
	}

	DEBUG("TextChunks:"<<textChunks.size()<<"("<<textChunks[0].start<<" - "<<textChunks[0].end<<")")
}

GUIText::~GUIText()
{

}

void GUIText::Draw( int xOff , int yOff )
{
	char tmp[512];
	for(int i=0;i<textChunks.size();i++)
	{
		memset(tmp,0,sizeof(tmp));
		strncpy(tmp,str+textChunks[i].start,textChunks[i].end-textChunks[i].start);
		DEBUG(xOff+textChunks[i].offsetX)
		RECT font_rect;
		SetRect(&font_rect,xOff+textChunks[i].offsetX,yOff+textChunks[i].offsetY,200,32);
		font->DrawTextA(NULL,tmp,-1,&font_rect,DT_LEFT|DT_NOCLIP,textChunks[i].color);
	}
}

