const express = require('express');
const cors = require('cors');
const path = require('path');

const app = express();
const PORT = 3000;

// Enable CORS for API communication with worldserver
app.use(cors());

// Serve static files
app.use(express.static(__dirname));

// Default route - serve index.html
app.get('/', (req, res) => {
    res.sendFile(path.join(__dirname, 'index.html'));
});

// Handle legacy /wsg-lobby paths and redirect to root
app.get('/wsg-lobby', (req, res) => {
    res.redirect('/' + (req.url.includes('?') ? req.url.substring(req.url.indexOf('?')) : ''));
});

app.get('/wsg-lobby/index.html', (req, res) => {
    res.redirect('/' + (req.url.includes('?') ? req.url.substring(req.url.indexOf('?')) : ''));
});

// Start server
app.listen(PORT, () => {
    console.log(`WSG Lobby Web Server running at http://localhost:${PORT}`);
    console.log(`API server should be running at http://localhost:8080`);
    console.log('');
    console.log('Access the lobby interface at: http://localhost:3000');
});