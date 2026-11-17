# Writes the licensing notes and the license as the RTF page the installer shows.
file(READ ${SOURCE}/NOTICE.md Notice)
file(READ ${SOURCE}/LICENSE License)
set(Text "${Notice}\n\n${License}")
string(REPLACE "\\" "\\\\" Text "${Text}")
string(REPLACE "{" "\\{" Text "${Text}")
string(REPLACE "}" "\\}" Text "${Text}")
string(REPLACE "\n" "\\par\n" Text "${Text}")
file(WRITE ${OUT} "{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0 Segoe UI;}}\\fs18 ${Text}}")
