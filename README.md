embeded page and simple text substituion.
=====================================

A very small template embedded engine for C++. It has absolute minimum function but yet powerful. It basically has 3 types of value, string, number and list. User could use Variable to enable runtime feature. Therefore , syntatically, it has 4 types. 

String is just quoted string , it will be converted into the output without moditification (well,except escape characters). Number will be converted into the string and the list that is included inside of other list will be flatten. Eg , [1,2,3,[4,5]] is actually [1,2,3,4,5].

eg:
[1,2,3,"Hello World"] --> 123HelloWorld.
Also suppose you register a variable called P inside of the C++ which has value "ABD", then the follwing example :
[P,1] ---> ABD1

A special structure called post processor has been invented to enable user achieving fancy output. User could optionally append a "{}" body which contains any text that will be output. 
Eg:
[1,2] { AABBCCD $ EFG } will output:

AABBCCD 1 EFG AABBCCD 2 EFG 

As you see, the dollar sign will be substitued based on the context. If the prefix is a string or number, then it is a string or number ; if the prefix expression is an array, then every value inside of the array will be outputed once. Therefore user could establish an implicit loop.

Range value is supported inside of the array, therefore [1-10] will be evaluated to list [1,2,3,4....,10] which will help to save your typing.

Lastly, section feature is also supportted. A section serve as a conditional entry for specific template sentence. If a section is on, then all the template sentence that is belonged to this section will be evaluated, otherwise will be skipped.

Eg:
<"SectionName" [1,2,Var1]{$AAA} >

This sentence forms a section based sentence. The section name is followed by section start symbol <. Var1 is registered inside of C++ howevere only under the section "SectionName". In C++, if user disable this section, this sentence will be skiped causing nothing output; otherwise it will be evaluated accordingly. Section serves as a conditional method for text substituion.

How to embed such template sentence ?Easy, using backtick. 
Eg:
```<HTML> <Head>`MyHead`</Head> </HTML>```

Then this text will be evaluated to replace MyHead to corresponding value in C++. If you want to have backtick inside of your HTML, just use ```\```` to escape it, don't forget \ itself needs to be escaped as well. 

In side of the post process body, it is allowed to have a recursive code body . Eg :

`[1,2,3] { <table> $ <tr>`[4,5]{$}`</tr> </table> } will be evaluated into <table>1<tr>45</tr></table><table>2<tr>45</tr></table>. You could nested recursive expression as much as you want and also recursive expression supports
section selector as well. 

Have fun :)






