# single_line_editor
Single Line Editor (SLED) is a simple editor to edit a single line of a file directly in the command line prompt.

## Description:
SLED is reading the first line of a file or from the pipe and allowed a quick inline text modification.
The result can be written in the same file, another file, to stdout or both. 

SLED is very polyvalent and can replace tee, cp, echo, cat, ... in many scenarios.

## Usage:
~~~
sled [options] [file]
~~~
### Options:
- -b: Bypass editing and print directly
- -p: Print the line to stdout
- -n: Print without trailing newline
- -f \<file path>: Specify an alternative output file
- -m: Write to file without trailing newline
- -h: Print help

### Editing:
- Left and right key to move on the line.
- Enter key to validate
- Escape key to cancel and closing the editor.

## Example:
### Edit the first line of a file
~~~bash
sled myfile.txt
~~~
### Tee after editing
~~~bash
sled -p myfile.txt
~~~
### Copy first line of a file to another file
~~~bash
sled -b -f destination.txt source.txt
~~~
### write a text to a new file
~~~bash
sled -b -f destination.txt
~~~
### grep a file, edit the result line and write the result in a new file
~~~bash
cat /proc/cpuinfo | grep "model name" | sled -f cpu.name
~~~  

## Bugs
- user key entries over pipe write 2 times the result in the terminal.
- reading tab characters.

## Todo
- tab
- Moving between lines with key up and down.
- Regex to find a line to edit.