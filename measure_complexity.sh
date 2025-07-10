# Host code
echo "microShell Host application Complexity Measurement"
###############################################################

echo "Audio Compression"
scc  examples_sw/apps/audio

echo "Digital Signature"
scc  examples_sw/apps/digi_sign

echo "Secure Storage"
scc  examples_sw/apps/secure

echo "Secure Storage 2"
scc  examples_sw/apps/secure2

echo "Speech Recognition"
scc  examples_sw/apps/speech



###############################################################