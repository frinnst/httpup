////////////////////////////////////////////////////////////////////////
// FILE:        argparser.cpp
// AUTHOR:      Johannes Winkelmann, jw@tks6.net
// COPYRIGHT:   (c) 2004 by Johannes Winkelmann
// ---------------------------------------------------------------------
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation; either version 2 of the License, or
//  (at your option) any later version.
////////////////////////////////////////////////////////////////////////

#include <iostream>
#include <sstream>
#include <cassert>
#include <libgen.h>

#include "argparser.h"

using namespace std;

ArgParser::ArgParser()
    : m_cmdIdCounter(0),
      m_optIdCounter(0)
{
}

ArgParser::~ArgParser()
{
    map<int, Option*>::iterator oit = m_options.begin();
    for (; oit != m_options.end(); ++oit) {
        delete oit->second;
    }

    map<string, Command*>::iterator cit = m_commands.begin();
    for (; cit != m_commands.end(); ++cit) {
        delete cit->second;
    }
}

int ArgParser::addCommand(APCmd& cmd,
                          const std::string& name,
                          const std::string& description,
                          ArgNumberCheck argNumberCheck,
                          int argNumber,
                          const std::string& otherArguments)
{
    Command* command = new Command;

    ++m_cmdIdCounter;
    cmd.id = m_cmdIdCounter;

    command->apCmd = &cmd;
    command->id = m_cmdIdCounter;
    command->name = name;
    command->argNumber = argNumber;
    command->argNumberCheck = argNumberCheck;

    command->description = description;
    command->otherArguments = otherArguments;

    m_commands[name] = command;
    m_commandIdMap[cmd.id] = command;


    APCmd apcmd;
    apcmd.id = m_cmdIdCounter;

    PREDEFINED_CMD_HELP.init("help", 'h', "Print this help message");

    // add predefined commands
    addOption(cmd, PREDEFINED_CMD_HELP, false);

    return 0;
}

int ArgParser::addOption(const APCmd& commandKey,
                         APOpt& key,
                         bool required)
{
    // TODO: check for null cmd
    if (m_commandIdMap.find(commandKey.id) == m_commandIdMap.end()) {
        return -1;
    }

    Option* o = 0;
    if (key.id != -1 && m_options.find(key.id) != m_options.end()) {
        o = m_options.find(key.id)->second;
    }

    if (!o) {

        assert(key.m_initialized == true);

        o = new Option();
        ++m_optIdCounter;
        key.id = m_optIdCounter;

        o->id = key.id;
        o->description = key.m_description;
        o->requiresValue = key.m_valueRequired;
        o->shortName = key.m_shortName;
        o->longName = key.m_longName;
        o->valueName = key.m_valueName;

        if (key.m_shortName != 0) {
            m_optionsByShortName[key.m_shortName] = o;
        }
        if (key.m_longName != "") {
            m_optionsByLongName[key.m_longName] = o;
        }

        m_options[key.id] = o;

    }


    Command* cmd = m_commandIdMap[commandKey.id];
    if (required) {
        cmd->mandatoryOptions[key.id] = o;
    } else {
        cmd->options[key.id] = o;
    }


    return 0;
}


void ArgParser::parse(int argc, char** argv)
{
    bool commandFound = false;
    string command = "";
    Command* cmd = 0;
    int cmdPos = 0;

    m_appName = basename(argv[0]);

    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] != '-') {
            if (!commandFound) {
                if (m_commands.find(argv[i]) == m_commands.end()) {
                    parseError("Non option / Non command argument '" +
                               string(argv[i]) + "'");
                }

                cmd = m_commands[argv[i]];
                m_command.id = cmd->apCmd->id;
                commandFound = true;
                cmdPos = i;
                break;
            }
        } else {
            // TODO: add proper handling for global options

            string arg = argv[i];
            if (arg == "-h" || arg == "--help") {
                cout << generateUsage() << endl;
                exit(0);
            }
        }

    }

    if (!commandFound) {
        parseError("No command used");
        exit(-1);
    }

    for (int i = 1; i < argc; ++i) {

        if (i == cmdPos) {
            continue;
        }

        if (argv[i][0] == '-') {
            if (argv[i][1] == '\0') {
                parseError("Illegal token: '-'", cmd->name);
            } else if (argv[i][1] == '-') {

                char* valPtr = strchr(argv[i]+2, '=');
                if (valPtr) {
                    *valPtr = '\0';
                    ++valPtr;
                }

                if (m_optionsByLongName.find(argv[i]+2) ==
                    m_optionsByLongName.end()) {
                    parseError("unknown option:" + string(argv[i]+2),
                               cmd->name);
                }

                Option* o = m_optionsByLongName[argv[i]+2];
                string val = "";
                if (o->requiresValue) {
                    if (valPtr == NULL || *valPtr == 0) {
                        parseError("Value required for option '" +
                                   string(argv[i]+2), cmd->name);
                    } else {
                        val = valPtr;
                    }
                }
                m_setOptions[o->id] = val;
            } else {
                if (argv[i][2] != '\0') {
                    parseError("invalid short option '" +
                               string(argv[i]+1) + "'", cmd->name);
                }

                if (m_optionsByShortName.find(argv[i][1]) ==
                    m_optionsByShortName.end()) {
                    parseError("unknown short option:" + string(argv[i]+1),
                               cmd->name);
                }

                Option* o = m_optionsByShortName[argv[i][1]];
                string val = "";
                if (o->requiresValue) {
                    if (i+1 == argc) {
                        parseError("Option required for option '" +
                                   string(argv[i]+1), cmd->name);
                    } else {
                        val = argv[i+1];
                        ++i;
                    }
                }
                m_setOptions[o->id] = val;
            }
        } else {
            m_otherArguments.push_back(string(argv[i]));
        }
    }

    if (isSet(PREDEFINED_CMD_HELP)) {
        cout << generateHelpForCommand(cmd->name) << endl;
        exit(0);
    } else {

        // make sure all required options of a command are set

        std::map<int, Option*>::iterator it;
        it = cmd->mandatoryOptions.begin();
        for (; it != cmd->mandatoryOptions.end(); ++it) {
            if (!isSet(it->second->id)) {
                parseError("Command '" + cmd->name +
                           "' requires option " +
                           string("-") + it->second->shortName +
                           string(" | ") +
                           string("--") + it->second->longName + " not found",
                           cmd->name);
            }
        }
    }

    switch (cmd->argNumberCheck)
    {
        case EQ:
            if (m_otherArguments.size() != cmd->argNumber) {
                ostringstream ostr;
                ostr << cmd->name
                     << " takes exactly "
                     << cmd->argNumber
                     << (cmd->argNumber == 1 ? " argument." : " arguments.");

                parseError(ostr.str(), cmd->name);
            }
            break;
        case MIN:
            if (m_otherArguments.size() < cmd->argNumber) {
                ostringstream ostr;
                ostr << cmd->name
                     << " takes at least "
                     << cmd->argNumber
                     << (cmd->argNumber == 1 ? " argument." : " arguments.");

                parseError(ostr.str(), cmd->name);
            }
            break;
        case MAX:
            if (m_otherArguments.size() > cmd->argNumber) {
                ostringstream ostr;
                ostr << cmd->name
                     << " takes at most "
                     << cmd->argNumber
                     << (cmd->argNumber == 1 ? " argument." : " arguments.");

                parseError(ostr.str(), cmd->name);
            }
            break;
        case NONE:
        default:
            break;
    }
}

void ArgParser::parseError(const string& error, const string& cmd) const
{
    cerr << "Parse error: " << error << endl;
    if (cmd != "") {
        cerr << generateHelpForCommand(cmd) << endl;
    } else {
        cerr << generateUsage() << endl;
    }
    exit(-1);
}

ArgParser::APCmd ArgParser::command() const
{
    return m_command;
}

bool ArgParser::isSet(const APOpt& key) const
{
    return isSet(key.id);
}

bool ArgParser::isSet(int key) const
{
    return m_setOptions.find(key) != m_setOptions.end();
}


std::string ArgParser::getOptionValue(const APOpt& key) const
{
    return m_setOptions.find(key.id)->second;
}

std::string ArgParser::appName() const
{
    return m_appName;
}

std::string ArgParser::generateHelpForCommand(const std::string& command) const
{
    std::map<std::string, Command*>::const_iterator cit =
        m_commands.find(command);

    if (cit == m_commands.end()) {
        return "";
    }

    const Command * const  cmd = cit->second;
    string help = "";;

    help += "command '" + cmd->name + " " + cmd->otherArguments + "'\n";
    help += "  " + cmd->description;
    help += "\n\n";


    std::map<int, Option*>::const_iterator it =
    it = cmd->mandatoryOptions.begin();
    if (it != cmd->mandatoryOptions.end()) {
        help += "  Required: \n";
        for (; it != cmd->mandatoryOptions.end(); ++it) {
            help += generateOptionString(it->second);
        }
    }

    it = cmd->options.begin();
    if (it != cmd->options.end()) {
        help += "  Optional: \n";
        for (; it != cmd->options.end(); ++it) {
            help += generateOptionString(it->second);
        }
    }

    return help;
}

string ArgParser::generateOptionString(Option* o) const
{
    string help = "    ";

    if (o->shortName) {
        help += "-";
        help += o->shortName;

        if (o->requiresValue && o->valueName != "") {
            help += " " + o->valueName;
        }

        help += " | ";
    }

    if (o->longName != "") {
        help += "--";

        help += o->longName;
        if (o->requiresValue && o->valueName != "") {
            help += "=" + o->valueName;
        }

        help += "    ";
        help += o->description;
        help += "\n";
    }

    return help;
}

std::string ArgParser::generateUsage() const
{
    string usage = getAppIdentification() +
        "USAGE: " + m_appName +
        " [OPTIONS] command <arguments>\n\n";
    usage += "  Where command is one of the following:\n";

    std::map<std::string, Command*>::const_iterator it;
    it = m_commands.begin();
    for (; it != m_commands.end(); ++it) {
        usage += "    " + it->first + "\t\t" +
            it->second->description + "\n";
    }

    return usage;
}

const std::vector<std::string>& ArgParser::otherArguments() const
{
    return m_otherArguments;
}
