// --- CONFIGURAÇÕES ---
const BROKER_URL = 'ws://localhost:9001';
const TOPIC = 'teste/arduino';

const options = {
    username: 'user',     
    password: 'password',
    keepalive: 60
};

// Mapa de Cores para o Fluxo (Texto)
const flowColors = {
    'LIVRE': '#2ecc71',     // Verde
    'LEVE': '#2ecc71',      // Verde
    'MODERADO': '#f1c40f',  // Amarelo
    'INTENSO': '#e74c3c',   // Vermelho
    'PARE': '#e74c3c'       // Vermelho (para estado de parada)
};

// Variáveis de Estatística
let stats = { total: 0, intense: 0, amb: 0, ped: 0, peakTime: '--:--' };

// Variável para evitar contagem múltipla do botão
let estadoPedestreAnterior = false; 

// --- INICIALIZAÇÃO DO GRÁFICO ---
const ctx = document.getElementById('mainChart').getContext('2d');
let mainChart = new Chart(ctx, {
    type: 'line',
    data: {
        labels: [],
        datasets: [{
            label: 'Fluxo de Tráfego',
            data: [],
            borderColor: '#005492', // Azul PCR
            backgroundColor: 'rgba(0, 84, 146, 0.1)',
            fill: true,
            tension: 0.3, // Suavização da linha
            pointRadius: 2
        }]
    },
    options: { 
        responsive: true, 
        maintainAspectRatio: false, // Permite que o gráfico ocupe a altura definida no CSS
        animation: { duration: 0 }, // Desliga animação para performance máxima em tempo real
        scales: {
            y: {
                min: 0,
                max: 4,
                ticks: { 
                    stepSize: 1,
                    callback: function(value) {
                        if(value === 1) return 'LIVRE';
                        if(value === 2) return 'MODERADO';
                        if(value === 3) return 'INTENSO';
                        return '';
                    }
                }
            },
            x: {
                ticks: { maxTicksLimit: 10 } // Evita poluição no eixo X
            }
        },
        plugins: {
            legend: { display: false } // Esconde a legenda para limpar o visual
        }
    }
});

// --- CONEXÃO MQTT ---
const client = mqtt.connect(BROKER_URL, options);

client.on('connect', () => {
    console.log("✅ Conectado ao Broker!");
    const statusEl = document.getElementById('mqtt-status');
    statusEl.innerHTML = '<i class="fas fa-circle" style="font-size: 8px;"></i> OPERACIONAL - COP RECIFE';
    statusEl.style.color = "#2ecc71"; // Verde
    client.subscribe(TOPIC);
});

client.on('message', (topic, message) => {
    try {
        const data = JSON.parse(message.toString());
        // requestAnimationFrame garante sincronia com a taxa de atualização do monitor
        requestAnimationFrame(() => {
            updateUI(data);
            processIntelligence(data);
        });
    } catch (e) { 
        console.error("❌ Erro ao processar JSON:", e); 
    }
});

// --- FUNÇÕES DE ATUALIZAÇÃO ---
function updateUI(data) {
    const s1Status = document.getElementById('s1-status');
    const s2Status = document.getElementById('s2-status');

    // 1. ATUALIZAÇÃO DAS LUZES (Visual do Semáforo)
    resetLights(); // Apaga tudo antes de acender o correto

    if(data.estado.includes('S1_VERDE')) { 
        setActive('s1', 'g'); setActive('s2', 'r'); 
    }
    else if(data.estado.includes('S2_VERDE')) { 
        setActive('s2', 'g'); setActive('s1', 'r'); 
    }
    else if(data.estado.includes('S1_AMARELO')) { 
        setActive('s1', 'y'); setActive('s2', 'r'); 
    }
    else if(data.estado.includes('S2_AMARELO')) { 
        setActive('s2', 'y'); setActive('s1', 'r'); 
    }
    else { 
        // Caso VERMELHO_TOTAL ou qualquer outro estado de segurança
        setActive('s1', 'r'); setActive('s2', 'r'); 
    }

    // 2. ATUALIZAÇÃO DOS TEXTOS DE STATUS
    if (data.estado.includes('S1')) {
        // S1 Aberto
        updateText(s1Status, data.transito, flowColors[data.transito]);
        updateText(s2Status, "AGUARDANDO", "#95a5a6");
    } else if (data.estado.includes('S2')) {
        // S2 Aberto
        updateText(s2Status, data.transito, flowColors[data.transito]);
        updateText(s1Status, "AGUARDANDO", "#95a5a6");
    } else {
        // Tudo Vermelho (Modo Pedestre)
        updateText(s1Status, "PARE (PEDESTRE)", "#e74c3c");
        updateText(s2Status, "PARE (PEDESTRE)", "#e74c3c");
    }

    // 3. ATUALIZAÇÃO DO GRÁFICO
    const map = { 'LIVRE': 1, 'LEVE': 1, 'MODERADO': 2, 'INTENSO': 3 };
    const now = new Date().toLocaleTimeString().substring(0, 8);
    
    // Só adiciona ponto se tiver dado válido, senão assume 0
    mainChart.data.labels.push(now);
    mainChart.data.datasets[0].data.push(map[data.transito] || 0);

    // Mantém histórico curto (últimos 20 pontos) para efeito de "rolagem"
    if(mainChart.data.labels.length > 20) {
        mainChart.data.labels.shift();
        mainChart.data.datasets[0].data.shift();
    }
    mainChart.update('none'); // Update rápido sem animação

    // Log lateral
    addLog(`Estado: ${data.estado} | Fluxo: ${data.transito}`);
}

function processIntelligence(data) {
    stats.total++;
    
    // --- LÓGICA DE CONTADOR DE PEDESTRES (BORDA DE SUBIDA) ---
    // Conta apenas se mudou de false para true
    if (data.pedestre === true && estadoPedestreAnterior === false) {
        stats.ped++;
        addLog("🚸 Pedestre solicitou travessia");
    }
    estadoPedestreAnterior = data.pedestre; 
    // --------------------------------------------------------

    if (data.ambulancia) stats.amb++;
    
    if (data.transito === 'INTENSO') {
        stats.intense++;
        stats.peakTime = new Date().toLocaleTimeString().substring(0,5);
    }

    const retençao = stats.total > 0 ? Math.round((stats.intense / stats.total) * 100) : 0;
    
    // Atualiza Painel de Inteligência
    document.getElementById('rep-retencao').textContent = retençao + "%";
    document.getElementById('rep-pico').textContent = stats.peakTime;
    document.getElementById('rep-amb').textContent = stats.amb;
    document.getElementById('rep-ped').textContent = stats.ped;
}

// --- UTILITÁRIOS ---

function updateText(element, text, color) {
    element.textContent = text;
    element.style.color = color || '#333';
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
    const time = new Date().toLocaleTimeString();
    li.innerHTML = `<small>[${time}]</small> ${msg}`;
    
    const list = document.getElementById('log-list');
    list.prepend(li);
    
    // Limita o log a 30 itens para não pesar a memória
    if(list.children.length > 30) list.lastChild.remove();
}

function exportToCSV() {
    let csv = "data:text/csv;charset=utf-8,--- RELATORIO CTTU ---\n";
    csv += `Data,${new Date().toLocaleDateString()}\n`;
    csv += `Retencao,${document.getElementById('rep-retencao').textContent}\n`;
    csv += `Horario de Pico,${stats.peakTime}\n`;
    csv += `Total Pedestres,${stats.ped}\n\n`;
    csv += "Horario,Evento\n";

    document.querySelectorAll('#log-list li').forEach(li => {
        csv += `${li.innerText}\n`;
    });

    const link = document.createElement("a");
    link.setAttribute("href", encodeURI(csv));
    link.setAttribute("download", `relatorio_cttu_${new Date().getTime()}.csv`);
    document.body.appendChild(link);
    link.click();
    document.body.removeChild(link);
}