// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.
using System;
using System.Collections.Generic;
using System.Text;
using AutomationTool;

class TestSnippetUtils : CommandUtils
{
	public static void Test(params object[] Args)
	{
		LogConsole("Args[0]=" + Args[0].ToString());
	}
}