#!/bin/python3

from lxml import etree

# Parse the XML file
tree = etree.parse('tmp/manpages/smb.conf.5.xml')
root = tree.getroot()

def pathSplit(path):
    dirlist = path.split('/')
    return dirlist



############### common command #################
def get_debuglevel():
    return "_comp_compgen -- -W \'{0..10}\'\n"

def get_directory():
    return "_comp_compgen_filedir -d \n"

def get_configfile():
    return get_directory()

def get_logbasename():
    return get_directory()

def get_OptionArgCommands():
    return ""

def get_list():
    #rabi check this function it should return something from smbtree
    return None

def get_nameresolve():
    #you need to read smb.conf file to update data
    return None


def get_netbiosname():
    return None

def get_netbiosscope():
    return None

def get_workgroup():
    #rabi you need to retrive data from smbtree
    return None

def get_realm():
    return None

def get_chown():
    return None

def get_chgrp():
    return None

def get_inherit():
    return None

def get_save():
    return None

def get_restore():
    return None

def get_querysecurityinfo():
    return None


def get_user():
    return None

def get_password():
    return None

def get_authenticationfile():
    return get_directory()

def get_usekerberos():
    return "_comp_compgen -- -W 'desired required off'\n"

def get_simplebinddn():
    return None

def get_usekrb5ccache():
    return None

def get_clientprotection():
    return "_comp_compgen -- -W 'sign encrypt off'\n"



def getSigningOption():
    return "_comp_compgen -- -W 'on off required'\n"

def getHost():
    return None


def getSectionfromAnchor(anchorAttrib):
    sectionLists = root.xpath('//section')
    for section in sectionLists:
        if ((section == None) or (section == '')):
            continue
        anchorList = section.xpath('./anchor')
        for anchor in anchorList:
            if (anchor.attrib['id'] == anchorAttrib):
                return section

def getValListFromSectionPath(section, *pathList):
    nextPath = None
    itemList = []
    #to understand this loop go though smb.conf.xml tags
    #varlistentry/listitem/itemizedlist/listitem/para

    currPath = section
    for path in pathList:
        nextPath = currPath.xpath(path)

        print(nextPath)
        currPath = nextPath

    for data in pathList:
        print(data)
        itemList.append(data)

def get_socketoptions():
    return None
#you need to read smb.conf file to update data
#    section = getSectionfromAnchor('SOCKETOPTIONS')
#    getValListFromSectionPath(section,'./variablelist/varlistentry/listitem/itemizedlist', './listitem/para')


get_socketoptions()

'''
        for var in variableList:
            subList = var.xpath('./'+ tag)
            if ((subList != None) or (subList != '')):
                tagStack.append(var)
                variableList = subList
            else:
                #else you need to pop an element from stack and traverse each element
                print('append is not done')
                continue

#vallist = getSocketOptions()



#for val1 in vallist:
#    print(val1.text)

def getValList(section ):
    valList = []
    variablelists = section.xpath('./variablelist')
    for variablelist in variablelists:
        if ((variablelist == None) or (variablelist == '')):
            continue
        varlistentries = variablelist.xpath('./varlistentry')

        for varlistentry in varlistentries:
            if ((varlistentry == None) or (varlistentry == '')):
                continue
            listitems = varlistentry.xpath('./listitem')

            for listitem in listitems:
                if ((listitem == None) or (listitem == '')):
                    continue
                itemizedlists = listitem.xpath('./itemizedlist')

                for itemizedlist in itemizedlists:
                    if ((itemizedlist == None) or (itemizedlist == '')):
                        continue
                    listitems2 = itemizedlist.xpath('./listitem')

                    for listitem2 in listitems2:
                        if ((listitem2 == None) or (listitem2 == '')):
                            continue

                        paras = listitem2.xpath('./para')

                        for para in paras:
                            if ((para.text == None) or (para.text == '')):
                                continue
                            valList.append(para)
    return valList














    for var  in tagStack:
        if((var == None) or (var == '')):
            continue            
        print(var[0].text)


    variablelists = section.xpath('./variablelist')
    for variablelist in variablelists:
        if ((variablelist == None) or (variablelist == '')):
            continue
        varlistentries = variablelist.xpath('./varlistentry')

        for varlistentry in varlistentries:
            if ((varlistentry == None) or (varlistentry == '')):
                continue
            listitems = varlistentry.xpath('./listitem')

            for listitem in listitems:
                if ((listitem == None) or (listitem == '')):
                    continue
                itemizedlists = listitem.xpath('./itemizedlist')

                for itemizedlist in itemizedlists:
                    if ((itemizedlist == None) or (itemizedlist == '')):
                        continue
                    listitems2 = itemizedlist.xpath('./listitem')

                    for listitem2 in listitems2:
                        if ((listitem2 == None) or (listitem2 == '')):
                            continue

                        paras = listitem2.xpath('./para')

                        for para in paras:
                            if ((para.text == None) or (para.text == '')):
                                continue
                            valList.append(para)
    return valList
'''




'''
def getValListFromAnchor( anchorAttrib):
    sectionLists = root.xpath('//section')
    for section in sectionLists:
        if ((section == None) or (section == '')):
            continue
        anchorList = section.xpath('./anchor')
        for anchor in anchorList:
            if (anchor.attrib['id'] == anchorAttrib):
                return getValList(section)

def getValListFromSectionPath(section, **path):
    valList = []
    tagStack = []
    tagList = pathSplit(path)
    variableList = section.xpath('./variablelist')
    tagStack.append(variableList)

    #to understand this loop go though smb.conf.xml tags
    #varlistentry/listitem/itemizedlist/listitem/para
    for tag in tagList:
        for var in variableList:
            subList = var.xpath('./'+ tag)
            if ((subList != None) or (subList != '')):
                tagStack.append(var)
                variableList = subList
                    #print( subList[0].tag)
            else:
                #else you need to pop an element from stack and traverse each element
                print('append is not done')
                continue
#    for tegs in tagStack:
#        print(tegs[0].tag)

def getSectionfromAnchor(anchorAttrib):
    sectionLists = root.xpath('//section')
    for section in sectionLists:
        if ((section == None) or (section == '')):
            continue
        anchorList = section.xpath('./anchor')
        for anchor in anchorList:
            if (anchor.attrib['id'] == anchorAttrib):
                return section

def get_socketoptions():
    section = getSectionfromAnchor('SOCKETOPTIONS')
    getValListFromSectionPath(section,'varlistentry/listitem/itemizedlist/listitem/para')
'''
