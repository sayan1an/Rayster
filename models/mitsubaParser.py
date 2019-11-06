import xml.etree.ElementTree as ET

filename = "./spaceship/scene.xml"

tree = ET.parse(filename)
root = tree.getroot()

# return the atrributes of a tag
# default: search for first occurence of a tag 
def searchTags(tagName, root, index=0):
    counter = 0
    for child in root:
        if child.tag == tagName and counter == index:
            counter = counter + 1
            return child.attrib
    return {}

for child in root:
    if child.tag == 'shape' and child.attrib["type"] =='obj':
        stringAttrib = searchTags("string", child)
        refAtrib = searchTags("ref", child)
        try :
            meshName = stringAttrib["value"] 
            materialId = refAtrib["id"]        
        except :
            continue
        print("addInstance(\"" + materialId + "\", " + str(int(meshName[11:-4])) + ");")