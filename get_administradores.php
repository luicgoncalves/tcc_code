<?php
header('Content-Type: application/json');

$host = 'localhost';  // Altere conforme necessário
$user = 'user';       // Usuário do banco
$password = 'senha';       // Senha do banco
$database = 'banco';    // Nome do banco de dados

// Conectar ao banco de dados
$conn = new mysqli($host, $user, $password, $database);

// Verificar conexão
if ($conn->connect_error) {
    die(json_encode(["error" => "Falha na conexão com o banco de dados."]));
}

// Consulta SQL para buscar administradores
$sql = "SELECT idcard FROM vw_administradores";
$result = $conn->query($sql);

if (!$result) {
    die(json_encode(["error" => "Erro na consulta SQL."]));
}

$adminCards = [];
while ($row = $result->fetch_assoc()) {
    $adminCards[] = $row['idcard'];
}

$conn->close();

echo json_encode($adminCards);
