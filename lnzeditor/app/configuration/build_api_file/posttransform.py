
f=open('..\lnzscript.api','r')
txt = f.read()
f.close()

txt = txt.replace('[[[linebr]]]','\n').replace('[[br]]',' , ')

#trim lines that are hopelessly long
MAXLINE = 310
atxt = txt.replace('\r\n','\n').split('\n')
for i in range(len(atxt)):
	if len(atxt[i]) > MAXLINE:
		atxt[i] = atxt[i][0:MAXLINE] + '...'
txt = '\n'.join(atxt)

fout=open('..\lnzscript.api','w')
fout.write(txt)
fout.close()