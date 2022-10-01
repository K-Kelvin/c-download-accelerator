# C Download Accelerator

C program that downloads files from the internet using TCP+TLS connections. <br>
Implements threads to make the downloads parallel. <br>
Takes the following parameters: <br>
&nbsp;&nbsp; -u Https url of the object to be downloaded <br>
&nbsp;&nbsp; -n Number of parts to download the object <br>
&nbsp;&nbsp; -o Path to output file <br>

## To compile

`$ make `

## To run

`$ make run1 `

or <br>
`$ ./http_downloader -u "https://cobweb.cs.uga.edu/~perdisci/CSCI6760-F21/Project2-TestFiles/topnav-sport2_r1_c1.gif" -o topnav-sport2_r1_c1.gif -n 5 `
