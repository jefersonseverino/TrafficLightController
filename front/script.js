// --- CONFIGURAÇÕES ---
const BROKER_URL = 'ws://localhost:9001';
const TOPIC = 'teste/arduino';

const options = {
    username: 'user',     
    password: 'password',
    keepalive: 60
};

// Mapa de Cores para o Fluxo
const flowColors = {
    'LIVRE': '#2ecc71',
    'LEVE': '#2ecc71',
    'MODERADO': '#f1c40f',
    'INTENSO': '#e74c3c'
};

// Variáveis de Estatística
let stats = { total: 0, intense: 0, amb: 0, ped: 0, peakTime: '--:--' };

// --- INICIALIZAÇÃO DO GRÁFICO ---
const ctx = document.getElementById('mainChart').getContext('2d');
let mainChart = new Chart(ctx, {
    type: 'line',
    data: {
        labels: [],
        datasets: [{
            label: 'Volume de Tráfego (S1)',
            data: [],
            borderColor: '#005492',
            backgroundColor: 'rgba(0, 84, 146, 0.1)',
            fill: true,
            tension: 0.3
        }]
    },
    options: { 
        responsive: true, 
        maintainAspectRatio: false, // ESSENCIAL: Permite que o gráfico use a altura do CSS
        animation: { duration: 300 }, // Reduz animação para evitar lag
        scales: {
            y: {
                min: 0,
                max: 4,
                ticks: { stepSize: 1 }
            }
        }
    }
});

// --- CONEXÃO MQTT ---
const client = mqtt.connect(BROKER_URL, options);

client.on('connect', () => {
    console.log("✅ Conectado ao Broker!");
    document.getElementById('mqtt-status').innerHTML = "● OPERACIONAL - COP RECIFE";
    document.getElementById('mqtt-status').style.color = "#2ecc71";
    client.subscribe(TOPIC);
});

client.on('message', (topic, message) => {
    try {
        const data = JSON.parse(message.toString());
        // A ordem aqui importa: Primeiro o visual leve, depois o gráfico pesado
        updateUI(data);
        processIntelligence(data);
    } catch (e) { 
        console.error("❌ Erro ao processar JSON:", e); 
    }
});

// --- FUNÇÕES DE ATUALIZAÇÃO ---
function updateUI(data) {
    const s1Status = document.getElementById('s1-status');
    const s2Status = document.getElementById('s2-status');

    // 1. Atualiza Luzes (Sempre primeiro)
    resetLights();
    if(data.estado.includes('S1_VERDE')) { setActive('s1', 'g'); setActive('s2', 'r'); }
    else if(data.estado.includes('S2_VERDE')) { setActive('s2', 'g'); setActive('s1', 'r'); }
    else if(data.estado.includes('S1_AMARELO')) { setActive('s1', 'y'); setActive('s2', 'r'); }
    else if(data.estado.includes('S2_AMARELO')) { setActive('s2', 'y'); setActive('s1', 'r'); }
    else { setActive('s1', 'r'); setActive('s2', 'r'); }

    // 2. Lógica para Status Texto S1 e S2
    // Se a mensagem for de S1, o S2 fica em "AGUARDANDO" e vice-versa
    if (data.estado.includes('S1')) {
        s1Status.textContent = data.transito;
        s1Status.style.color = flowColors[data.transito] || '#333';
        s2Status.textContent = "EM ESPERA";
        s2Status.style.color = "#95a5a6";
    } else if (data.estado.includes('S2')) {
        s2Status.textContent = data.transito;
        s2Status.style.color = flowColors[data.transito] || '#333';
        s1Status.textContent = "EM ESPERA";
        s1Status.style.color = "#95a5a6";
    }

    // 3. Atualiza Gráfico (Otimizado com update('none') para evitar lag)
    const map = { 'LIVRE': 1, 'LEVE': 1, 'MODERADO': 2, 'INTENSO': 3 };
    const now = new Date().toLocaleTimeString().substring(0, 8);
    
    mainChart.data.labels.push(now);
    mainChart.data.datasets[0].data.push(map[data.transito] || 0);

    if(mainChart.data.labels.length > 20) {
        mainChart.data.labels.shift();
        mainChart.data.datasets[0].data.shift();
    }
    mainChart.update('none'); // Update sem animação para resposta instantânea

    addLog(`Sinal: ${data.estado} | Fluxo: ${data.transito}`);
}

function processIntelligence(data) {
    stats.total++;
    if(data.ambulancia) stats.amb++;
    if(data.pedestre) stats.ped++;
    if(data.transito === 'INTENSO') {
        stats.intense++;
        stats.peakTime = new Date().toLocaleTimeString().substring(0,5);
    }

    const retençao = stats.total > 0 ? Math.round((stats.intense / stats.total) * 100) : 0;
    document.getElementById('rep-retencao').textContent = retençao + "%";
    document.getElementById('rep-pico').textContent = stats.peakTime;
    document.getElementById('rep-amb').textContent = stats.amb;
    document.getElementById('rep-ped').textContent = stats.ped;
}

function resetLights() {
    document.querySelectorAll('.light').forEach(l => l.classList.remove('active'));
}

function setActive(sem, color) {
    const el = document.getElementById(`${sem}-${color}`);
    if(el) el.classList.add('active');
}

function addLog(msg) {
    const li = document.createElement('li');
    li.innerHTML = `<strong>[${new Date().toLocaleTimeString()}]</strong> ${msg}`;
    const list = document.getElementById('log-list');
    list.prepend(li);
    if(list.children.length > 30) list.lastChild.remove();
}

function exportToCSV() {
    let csv = "data:text/csv;charset=utf-8,--- RELATORIO CTTU ---\n";
    csv += `Data,${new Date().toLocaleDateString()}\n`;
    csv += `Retencao,${document.getElementById('rep-retencao').textContent}\n`;
    csv += `Horario de Pico,${stats.peakTime}\n\n`;
    csv += "Horario,Evento\n";

    document.querySelectorAll('#log-list li').forEach(li => {
        csv += `${li.innerText.replace(']', ',')}\n`;
    });

    const link = document.createElement("a");
    link.setAttribute("href", encodeURI(csv));
    link.setAttribute("download", `relatorio_cttu_${new Date().getTime()}.csv`);
    document.body.appendChild(link);
    link.click();
    document.body.removeChild(link);
}