<?php
header('Content-Type: application/json');

$host = 'localhost';  // Altere conforme necessário
$user = 'user';       // Usuário do banco
$password = 'senha';       // Senha do banco
$database = 'banco';    // Nome do banco de dados

// Verificar se o parâmetro "codigo_dispositivo" foi passado
if (!isset($_GET['codigo_dispositivo'])) {
    die(json_encode(["error" => "Parâmetro codigo_dispositivo ausente."]));
}

$codigo_dispositivo = $_GET['codigo_dispositivo'];

// Conectar ao banco de dados
$conn = new mysqli($host, $user, $password, $database);

// Verificar conexão
if ($conn->connect_error) {
    die(json_encode(["error" => "Falha na conexão com o banco de dados."]));
}

// Consulta SQL para buscar pessoas autorizadas filtrando pelo código do dispositivo
$sql = "SELECT * FROM vw_pessoas_autorizadas WHERE dispositivo_codigo = ?";

$stmt = $conn->prepare($sql);
$stmt->bind_param("s", $codigo_dispositivo);
$stmt->execute();
$result = $stmt->get_result();

if ($result->num_rows > 0) {
    $authorizedUsers = [];
    while ($row = $result->fetch_assoc()) {
        $authorizedUsers[] = $row;
    }
    echo json_encode($authorizedUsers);
} else {
    echo json_encode(["message" => "Nenhum usuario autorizado encontrado para este dispositivo."]);
}

$stmt->close();
$conn->close();
