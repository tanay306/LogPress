import json

dict={}
with open("variables.json","r") as inputfile:
    variables = json.load(inputfile)
    for key in variables:
        if key not in dict:
            dict[key]={}
        for indx,var in enumerate(variables[key]):
            var = var.replace("\r","")
            dict[key][var]=indx
 
with open("dictionaries.json","+w") as outfile:
    json.dump(dict,outfile,indent="  ")