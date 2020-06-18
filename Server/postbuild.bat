copy bin\Release\net5.0\* ..\..\Client\lib\ 
cd ..\..\Client\
call vsce package
code --install-extension client-0.0.1.vsix