#include <QtScript>
#include "provide_script.h"
#include "print_function.h"
#include "provide_common.h"

#include "functions_expose.h"

// The way things are now, you can create multiple ProvideScript objects, but they will share a single "print" function.

ProvideScript::ProvideScript()
{
	// constructor
	launchorz_functions::util_au3init();
	launchorz_functions::util_nircmd_init();
	
	// the engine was already created
	// create objects
	launchorz_functions::AddGlobalObjects(&engine);
	
	// add the print function to the engine
	QScriptValue fnScriptPrint = engine.newFunction(g_ProvideScript_PrintFunction);
	engine.globalObject().setProperty("print", fnScriptPrint);
	
	// add the include function to the engine
	QScriptValue fnScriptInclude = engine.newFunction(g_ProvideScript_IncludeFunction);
	engine.globalObject().setProperty("include", fnScriptInclude);
	
	// add the alert function and confirm function.
	engine.evaluate("alert = function(s){return Dialog.alert('LnzScript',s);}\n confirm = function(s){return Dialog.askYesNo('LnzScript',s)==Dialog.YES;}");
	
	// add flag saying that we have not yet included standard JavaScript libraries
	engine.globalObject().setProperty("__includedstd__", QScriptValue(&engine,false));
}


StringResult ProvideScript::EvalScript(QString filename)
{
	QFile file(filename);
	file.open(QIODevice::ReadOnly);
	QString contents = file.readAll();
	file.close();
	
	if (contents.isEmpty()) // string was empty
		return StringResult("");
	
	return EvalString(contents);
}

// there should be something like this that returns a QScriptValue. This would be better.
// the problem is that there have to be two return values: What was the result? and Was there an exception?
StringResult ProvideScript::EvalString(QString contentsRaw)
{
	QString contents = contentsRaw; //processLiteralStrings(contentsRaw);
	// Set "main" flag signalling that this file was not included
	QScriptValue ret = engine.evaluate("var __name__ = 'main';" + contents);
	if (engine.hasUncaughtException())
	{
		int lineno = engine.uncaughtExceptionLineNumber();
		QString msg = ret.toString();
		QString readableError = msg.sprintf("%s, at %d", qPrintable(msg), lineno);
		return StringResult(readableError, 1); // set error flag to 1
		// clear exception here?  engine.clearExceptions();
	}
	else
	{
		// When evaluating, print out "0" and "false" but don't print "null" or "undefined"
		if (! ret.toBoolean() && ret.toString()!="false" && !ret.isNumber())
			return StringResult(ret.toString(), -1);
		else
			return StringResult(ret.toString(), 0); 
	}
}


void ProvideScript::addArgv(int argc, char *argv[])
{
	// arg0 is lnzscript, arg1 is /f
	if (argc <= 2) return;
	int offset = 2; // how many arguments to ignore
	
	QScriptValue ar = engine.newArray(argc - offset);
	for (int i=offset; i<argc; i++)
		ar.setProperty(i-offset, QScriptValue(&engine, argv[i]));
	engine.globalObject().setProperty("argv", ar);
}

QScriptValue g_ProvideScript_PrintFunction(QScriptContext *ctx, QScriptEngine *eng)
{
	if (ctx->argumentCount()!=1) return ctx->throwError("print takes one argument(s).");
	// we don't check if it is a string, because .toString() on numbers will work just fine.
	
	QString str = ctx->argument(0).toString();
	g_LnzScriptPrintCallback( &str );
	return eng->nullValue();
}

QScriptValue g_ProvideScript_IncludeFunction(QScriptContext *ctx, QScriptEngine *eng)
{
	if (ctx->argumentCount()!=1) return ctx->throwError("include takes one argument(s).");
	if (!ctx->argument(0).isString()) return ctx->throwError(QScriptContext::TypeError,"include: argument 0 is not a string");

	// note there is nothing to stop a file from including itself. That would be bad; let's not do that.
	QString strFilename = ctx->argument(0).toString();
	
	if (strFilename == "<std>")
	{
		// check if they have already been included:
		if (eng->globalObject().property("__includedstd__", QScriptValue::ResolveLocal).toBoolean()) return eng->nullValue();
		
		strFilename = launchorz_functions::get_base_directory() + "std.js";
		eng->globalObject().setProperty("__includedstd__", QScriptValue(eng,true));
	}
	
	QString contentsRaw;
	try
	{
		QFile file(strFilename);
		if (!file.exists()) return ctx->throwError("Could not include file - file does not exist.");
		file.open(QIODevice::ReadOnly);
		contentsRaw = file.readAll();
		file.close();
	}
	catch (...)
	{
		return ctx->throwError("Could not include file.");
	}
	QString contents = contentsRaw; //processLiteralStrings(contentsRaw);
	// Set flag signalling that this WAS included. Because it is declared var, it should be private.
	QScriptValue ret = eng->evaluate("var __name__ = 'included';" + contents); //if this throws an exception, print it in a way that can be understood.
	if (eng->hasUncaughtException())
	{
		int lineno = eng->uncaughtExceptionLineNumber();
		QString msg = ret.toString();
		QString readableError = msg.sprintf("%s, at %d", qPrintable(msg), lineno);
		g_LnzScriptPrintCallback( &readableError); // print readable error
		
		// do not reset the exception, so that the script aborts.
	}
	
	return eng->nullValue();
}

/*
UNTESTED
// Transforms @'c:\directory' into 'c:\\directory
// Pass in the string as const so that [ ] is faster.
// Major problem- what about quotes in comments? More complicated than initially thought.
// Do we have to strip all comments before parsing this?
QString ProvideScript::processLiteralStrings(const QString& strInput)
{
	if (strInput.find('@',0,true)==-1)
		return strInput; //no @ in the string, so don't do anything
	
	QString strParsed;
	strParsed.reserve( strInput.length() + 5); //allocate as much as the input + 5
	int len = 0;
	
	int level = 0; // quote level
	enum states {NO_QUOTE, SINGLE_QUOTE, DOUBLE_QUOTE, SINGLE_TRANSFORM, LINE_COMMENT, MULTI_COMMENT};
	#define SQUOTE '\''
	#define DQUOTE '"'
	#define BSLASH '\\'
	int state = NO_QUOTE;
	for (int i=0;i<strInput.length()-1; i++ )
	{
		if (state==LINE_COMMENT || state==MULTI_COMMENT)
		{
			if (strInput[i]=='\n' && state==LINE_COMMENT) state = NO_QUOTE;
			else if (strInput[i]=='*' && strInput[i+1]=='/' && state==MULTI_COMMENT) state = NO_QUOTE;
			
			strParsed[len++] = strInput[i];
		}
		else
		{
			if (strInput[i]=='@' && strInput[i+1]==SQUOTE && state==NO_QUOTE)
			{
				state = SINGLE_TRANSFORM;
				strParsed[len++] = SQUOTE;
				i ++; // skip over the next char!
			}
			else if (strInput[i] == BSLASH && state == SINGLE_TRANSFORM)
			{
				// add two backslashes!
				strParsed[len++] = BSLASH;
				strParsed[len++] = BSLASH;
			}
			else if (strInput[i]==SQUOTE)
			{
				if (state==NOQUOTE) state = SINGLE_QUOTE;
				else if (state==SINGLE_QUOTE) state = NO_QUOTE;
				else if (state==DOUBLE_QUOTE) state = DOUBLE_QUOTE; // no op
				else if (state==SINGLE_TRANSFORM) state = NO_QUOTE; // end the transform
				
				strParsed[len++] = SQUOTE; //regardless, write it over
			}
			else if (strInput[i]==DQUOTE)
			{
				if (state==NOQUOTE) state = DOUBLE_QUOTE;
				else if (state==SINGLE_QUOTE) state = SINGLE_QUOTE; // no op
				else if (state==DOUBLE_QUOTE) state = NO_QUOTE; // end quote
				else if (state==SINGLE_TRANSFORM) state = SINGLE_TRANSFORM; // no op
				
				strParsed[len++] = DQUOTE; //regardless, write it over
			}
			else if (strInput[i] == '/' && strInput[i+1] == '/' && state == NO_QUOTE)
			{
				state = LINE_COMMENT;
				strParsed[len++] = strInput[i];
			}
			else if (strInput[i] == '/' && strInput[i+1] == '*' && state == NO_QUOTE)
			{
				state = MULTI_COMMENT;
				strParsed[len++] = strInput[i];
			}
			else
			{
				strParsed[len++] = strInput[i];
			}
		}
	}
	strParsed[len++] = strInput[i]; //we don't transform the last one, anyways.
	return strParsed;
}


*/


/*

//atLeast: 0 is wrong number, 1 is too many, -1 is not enough
QScriptValue g_ProvideScript_ThrowException_WrongArgs(QScriptContext *ctx, char* functionName, int nExpected, int nAtLeast)
{
	QString strExceptionMessage;
	if (nAtLeast==0)
		strExceptionMessage.sprintf("Function %s takes exactly %d arguments.",functionName,nExpected);
	else if (nAtLeast==-1)
		strExceptionMessage.sprintf("Function %s takes at least %d arguments.",functionName,nExpected);
	else if (nAtLeast==1)
		strExceptionMessage.sprintf("Function %s takes at most %d arguments.",functionName,nExpected);
	return ctx->throwError(strExceptionMessage);
}

typedef paramtype_t int;
#define g_ProvideScript_ThrowException_WrongTypeBoolean 1
#define g_ProvideScript_ThrowException_WrongTypeInt 2
QScriptValue g_ProvideScript_ThrowException_WrongType(QScriptContext *ctx, char* functionName, int nArgument, paramtype_t nTypeExpected)
{
	static char typestable[] {
		"", "Boolean", "Integer", "String"
	};
	QString strExceptionMessage;
	assert nTypeExpected < 6 && nTypeExpected >=0
	strExceptionMessage.sprintf("Function %s: argument %d is not a %s.",functionName,typestable[nExpected]);
	
	return ctx->throwError(strExceptionMessage);
}

*/
