#pragma once
#include <QString>

#define MAX_CMD_COUNT (10)

class Helper
{
public:
	typedef void(*PfnCommand)();

	typedef struct
	{
		QString description;
		int cmdCount;
		PfnCommand pfnCmds[MAX_CMD_COUNT];
	} Command;
public:
	Helper() = delete;
	~Helper() = delete;

	static const Command cmdSo;
	static void Exit();
	static void ElfFixNormalSo();
	static void ElfFixDumpSoFromNormal();
	static bool elfDumpSoToNormal(QString &dumppath);
	static void ElfFixDumpSo();
	static bool elfFixSo(const char *sopath, const char *dumppath);
	static void ElfRebuild();
};

