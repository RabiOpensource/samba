#!/bin/python3
from lxml import etree
from autocompletehelper import *

# Define the XML file to parse
#xmlFile = 'tmp/manpages/smbcontrol.1.xml'
xmlFile = 'tmp/manpages/smbclient.1.xml'
#xmlFile = 'tmp/manpages/smbcacls.1.xml'
#xmlFile = 'tmp/manpages/smbget.1.xml'
#xmlFile = 'tmp/manpages/smbspool.8.xml'
#xmlFile = 'tmp/manpages/smbtar.1.xml'
#xmlFile = 'tmp/manpages/smbcquotas.1.xml'
#xmlFile = 'tmp/manpages/smbpasswd.5.xml'
#xmlFile = 'tmp/manpages/smbtree.1.xml'
#xmlFile = 'tmp/manpages/smbstatus.1.xml'


# Parse the XML file
tree = None
root = None

argStr = ''
functionStr = ''
subcommands = []
subNoArgCommands = []
# Create auto-completion code
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

# createFunctionName is for calling function
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
    


# createFunction is for function defination
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
    if (xmlFile != None):
        global tree, root
        tree = etree.parse(xmlFile)
        root = tree.getroot()


#################################################################################
#                                                                               #
#               Parsing XML data                                                #
#                                                                               #
#################################################################################

findRootAndTree(xmlFile)

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
    autoCompletionCode += f' | '

if subNoArgCommands:
    autoCompletionCode += f')\n'
    autoCompletionCode += f'            return\n'
    autoCompletionCode += f'            ;;\n\n'

autoCompletionCode += f'    esac\n'
autoCompletionCode += f'}}&& \n'
autoCompletionCode += f'  complete -F _comp_cmd_{command} {command}'


# Write the auto-completion code to a file
autoCompletionFile = f'{command}.sh'
with open(autoCompletionFile, 'w') as f:
    f.write(functionStr)
    f.write(autoCompletionCode)




















'''
            if (arg.text.find('|') > 0):
                fargument = arg.text.split('|')
                if (arg.text.find('=') > 0):
                    caseOption = fargument[1].split('=')[0]
                    fname = ((arg.text.split('--'))[1]).split('=')[0]
                    fData = createFunction(command, fname)
                    if (fData == None):
                        subnoargcommands.append(caseOption)
                    else:
                        functionStr = functionStr + fData
                        subcommands.append(caseOption)

                    argStr = argStr + ' ' + (fargument[1][:fargument[1].find('=') + 1])
                else:
                    argStr = argStr + ' ' + fargument[1]
            else:
                if (arg.text.find('=') > 0):
                    caseOption = arg.text.split('=')[0]
                    fname = ((arg.text.split('--'))[1]).split('=')[0]
                    fData = createFunction(command, fname)
                    if (fData == None):
                        subnoargcommands.append(caseOption)
                    else:
                        functionStr = functionStr + fData
                        subcommands.append(caseOption)
                    
                    argStr = argStr + ' ' + (arg.text[:arg.text.find('=') + 1])
                else:
                    argStr = argStr + ' ' + arg.text
'''
