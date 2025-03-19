#!/bin/python3

import sys
from lxml import etree
from autocompletehelper import *

option    = ""
outputdir = ""
xmlFile   = ""

tree = None
root = None

argStr = ''
functionStr = ''
subcommands = []
subNoArgCommands = []
autoCompletionCode = ''


#################################################################################
#                                                                               #
#               Creating Function of subcommand                                 #
#                                                                               #
#################################################################################

def makeSubCmdList(arg):
    global argStr
    if (arg.text.find('|') > 0):
        command = arg.text.split('|')
        for cmd in command:
            if ((cmd.find('--') >= 0) and (cmd.find('=') > 0)):
                argStr = argStr + ' ' + cmd[:cmd.find('=') + 1]
    else:
        if ((arg.text.find('--') > 0) or (arg.text.find('=') > 0)):
            argStr = argStr + ' ' + arg.text[:arg.text.find('=') + 1]

def createFunctionName(command, fName):
    if (fName.find('--') >= 0):
        fName = (fName.split('--')[1])
    if (fName.find('=') >= 0):
        fName = ((fName.split('='))[0])
    fName = fName.replace("-","")
    str =  '_comp_cmd_' + command + '_' + fName
    return str

def findCommandName(arg):
    subcmd = None
    if (arg.text.find('|') > 0):
        command = arg.text.split('|')
        for cmd in command:
            if ((cmd.find('--') >= 0) and (cmd.find('=') > 0)):
                subcmd = cmd.split('=')[0]
    else:
        if (arg.text.find('=') > 0):
            subcmd = arg.text.split('=')[0]
    return subcmd
    
def createFunction(command, fName):
    fName = fName.replace("-","")
    getFName = f"get_{fName}"
    try:
        getFunctionData = globals()[getFName]
        fdData = getFunctionData()
        if (fdData == None):
            return None
        else:
            fDefination =  '_comp_cmd_' + command + '_' + fName + '() \n{ \n'
            fDefination = fDefination + "   " + getFunctionData()
            fDefination = fDefination + '}\n'
            return fDefination
    except KeyError:
        return None

def findRootAndTree(xmlFile):
    if (xmlFile is not None):
        global tree, root
        tree = etree.parse(xmlFile)
        root = tree.getroot()


#################################################################################
#                                                                               #
#               Parsing XML data                                                #
#                                                                               #
#################################################################################
def usage():
    print( sys.argv[0] + " --output <output directory> <manpages/smbcommand.xml> ")
    sys.exit(1)

def initAutoCompleteScript():
    if len(sys.argv) != 4:
        usage()
    if ((sys.argv[1] == "--output") and (sys.argv[3] != None)):
        outputdir = sys.argv[2]
        xmlFile = sys.argv[3]
    else:
        usage()
        
def findListofSubCommandFromXml():
    for refsynopsisdiv in root.xpath('//refsynopsisdiv'):
        cmdsynopsis = refsynopsisdiv.find('cmdsynopsis')
        command = cmdsynopsis.find('literal').text
        args = cmdsynopsis.findall('arg')

        for arg in args:
            if 'choice' in arg.attrib:
                makeSubCmdList(arg)
                subcmd = findCommandName(arg)
                if (subcmd == None):
                    continue

                fName = subcmd.split('--')[1]
                fData = createFunction(command, fName)
                if (fData == None): 
                    subNoArgCommands.append(subcmd)
                else:
                    subcommands.append(subcmd)
                    functionStr = functionStr + fData


##################################################################################
#
#               Building auto complete script           
#
##################################################################################
def buildAutoCompleteScript():
    global autoCompletionCode
    autoCompletionCode += f'_comp_cmd_{command}() \n{{\n'
    autoCompletionCode += f'    local cur prev words cword was_split comp_args\n'
    autoCompletionCode += f'    cur="${{COMP_WORDS[COMP_CWORD]}}"\n'
    autoCompletionCode += f'    prev="${{COMP_WORDS[COMP_CWORD-1]}}"\n'
    autoCompletionCode += f'    words="{argStr}"\n'
    autoCompletionCode += f'    if [[ ${{cur}} == -* ]] ; then\n'
    autoCompletionCode += f'      COMPREPLY=( $(compgen -W "${{words}}" -- ${{cur}}) )\n'
    autoCompletionCode += f'          return 0\n'
    autoCompletionCode += f'    fi\n'

    autoCompletionCode += f'    case $prev in\n'

    for subcmd in subcommands:
        functionData = createFunctionName(command, subcmd)
        autoCompletionCode += f'        {subcmd})\n'
        autoCompletionCode += f'            {functionData}\n'
        autoCompletionCode += f'            return\n'
        autoCompletionCode += f'            ;;\n'

    autoCompletionCode += f'        '
    for noargcmd in subNoArgCommands:
        autoCompletionCode += f'{noargcmd}'
        if (noargcmd != subNoArgCommands[-1]):
            autoCompletionCode += f' | '

    if subNoArgCommands:
        autoCompletionCode += f')\n'
        autoCompletionCode += f'            return\n'
        autoCompletionCode += f'            ;;\n\n'

    autoCompletionCode += f'    esac\n'
    autoCompletionCode += f'}}&& \n'
    autoCompletionCode += f'  complete -F _comp_cmd_{command} {command}'

def writeAutoCompletionCodeToFile():
    # Write the auto-completion code to a file
    autoCompletionFile = f'{outputdir}'
    with open(autoCompletionFile, 'w') as f:
        f.write(functionStr)
        f.write(autoCompletionCode)


def main():
    initAutoCompleteScript() 
    findRootAndTree(xmlFile)
    findListofSubCommandFromXml()
    buildAutoCompleteScript()  
    writeAutoCompletionCodeToFile()

if __name__ == "__main__":
    main()
